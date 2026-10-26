#include "plants.h"
#include "maze.h"
#include "hud.h"
#include "guidemap.h" // MAX_MAP_COLS/MAX_MAP_ROWS
#include "menu.h"     // MENU_TILE_COUNT
#include "plant_tiles_gen.h"

// Detras de todo lo que ya reserva teselas, igual que se encadenan entre si.
// Son las de las plantas de ESTA habitacion, rehechas en cada una.
#define PLANT_TILE_BASE (TILE_USER_INDEX + MAZE_TILE_COUNT + GUIDEMAP_TILE_COUNT \
                         + MAZE_SCRATCH_TILE_COUNT + MENU_TILE_COUNT + HUD_FONT_TILE_COUNT)


// 3 independent lines per room (user request: "añade dos hileras más de
// plantas... en cada room" -- 2 more on top of the original 1). Also the
// width of collectedMask's per-room bitmask: PLANTS_LINES_PER_ROOM *
// PLANTS_MAX_PER_LINE bits must fit in collectedMask's u16 (15 of 16 used).
#define PLANTS_LINES_PER_ROOM 3
#define PLANTS_MAX_PER_LINE   5
#define PLANTS_LINE_MIN       3
// Una planta es una tesela de 8x8, y esa es su caja.
#define PLANTS_GLYPH_PX       8
// Random (anchor,axis,dir) tries before giving up on ONE line -- tombo
// rooms are ~90% open interior (maze.h's own doc comment), so this
// converges fast in practice, same reasoning as enemy.c's findOpenInLane.
#define PLANTS_SPAWN_ATTEMPTS 24
#if (PLANTS_PER_ROOM != (PLANTS_LINES_PER_ROOM * PLANTS_MAX_PER_LINE))
#error PLANTS_PER_ROOM no cuadra con las hileras de esta sala
#endif

#if (((PLANTS_CACHE_SLOTS + 1) * PLANTS_PER_ROOM) > PLANT_SHAPE_COUNT)
#error hay mas plantas que formas: subir PERFILES en tools/plant_tiles.py
#endif

static u8 curCol[PLANTS_LINES_PER_ROOM][PLANTS_MAX_PER_LINE];
static u8 curRow[PLANTS_LINES_PER_ROOM][PLANTS_MAX_PER_LINE];
static u8 curCount[PLANTS_LINES_PER_ROOM];

// Persists across room re-entries (like guidemap.c's own MapCell.enemyDead),
// but -- unlike enemyDead -- needs more than 1 bit per room, so it's a
// separate array here instead of packed into MapCell. Bit (line *
// PLANTS_MAX_PER_LINE + i) = that line's plant i (this room's
// curCol[line][i]/curRow[line][i], deterministic from roomSeed) already
// collected.
static u16 collectedMask[MAX_MAP_ROWS][MAX_MAP_COLS];

// The layout cache (see Plants_cacheCurrentRoom's doc comment in
// plants.h) -- the same three arrays above, one copy per map position.
// 33 bytes a room, 80 rooms at the biggest preset: ~2.6 KB, against a
// per-entry cost that showed up as a visible hitch every time the ship
// crossed a door.
static u8 cacheCol[PLANTS_CACHE_SLOTS][PLANTS_LINES_PER_ROOM][PLANTS_MAX_PER_LINE];
static u8 cacheRow[PLANTS_CACHE_SLOTS][PLANTS_LINES_PER_ROOM][PLANTS_MAX_PER_LINE];
static u8 cacheCount[PLANTS_CACHE_SLOTS][PLANTS_LINES_PER_ROOM];
static bool cacheValid[PLANTS_CACHE_SLOTS];
static u16 total;

// This planet's real plant count (Plants_setPlanetTotal) -- the
// denominator Plants_drawHud() prints, separate from `total` above
// (confusingly also named "total", but that one is the COLLECTED count).
// A per-planet constant, not session state, so Plants_reset() leaves it
// alone.
static u16 planetTotal;

// La habitacion cuyas plantas estan ahora en VRAM, y de donde sale el indice
// unico de cada una. PLANTS_CACHE_SLOTS cuando la celda no es habitacion.
static u16 curSlot = PLANTS_CACHE_SLOTS;
static u16 tilesSlot = 0xFFFF;
static u32 plantTileData[PLANTS_PER_ROOM * 8];   // u32: el DMA quiere alineado

// Own tiny xorshift PRNG, seeded from roomSeed, instead of the shared
// random() stream: Maze_generateRoom's accepted-attempt cache (maze.h)
// can replay a cached layout WITHOUT repeating its original search, so
// the shared stream's state after Maze_generateRoom returns is NOT
// reliably the same on every visit to the same room -- same reasoning
// enemy.c's own doc comment gives for avoiding it.
static u32 rngState;

static void seedRng(u16 roomSeed)
{
    u32 h = (u32) roomSeed;

    h = h * 2654435761u + 0x9E3779B9u; // distinct mixing constant from main.c's roomSeedFor/enemy.c's hashing
    h ^= h >> 13;
    rngState = h ? h : 1; // xorshift needs a nonzero state
}

static u32 rngNext(void)
{
    u32 x = rngState;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;

    return x;
}

// Unit cardinal steps, same 0=N/1=E/2=S/3=W convention Maze_slideEdge
// documents (doorOffsets[4]'s convention throughout this codebase).
static const s8 dirDX[4] = {  0, 1, 0, -1 };
static const s8 dirDY[4] = { -1, 0, 1,  0 };

// Generous upper bound on how many cells a single straight slide can ever
// cross in either axis: the larger of MAZE_MAX_W/MAZE_MAX_H, since a room
// may now be anything up to that (maze.h).
#define PLANTS_MAX_EDGE_LEN MAZE_MAX_W

// TRUE if (x,y) is already used by an earlier line placed THIS room
// (occCol/occRow[0..occCount-1]) -- checked while walking a candidate
// path for a LATER line, same way a real wall stops it (see
// Plants_spawnForRoom), so two lines can never land on the same cell.
// Linear scan: occCount is at most PLANTS_LINES_PER_ROOM *
// PLANTS_MAX_PER_LINE = 15, cheap either way.
static bool isOccupied(s16 x, s16 y, const u8 *occCol, const u8 *occRow, u8 occCount)
{
    u8 i;

    for (i = 0; i < occCount; i++)
        if ((occCol[i] == x) && (occRow[i] == y))
            return TRUE;

    return FALSE;
}

// Las celdas de la copa, una mascara de columnas por fila. Mismo dibujo que
// hace tools/plant_tiles.py: un ancho impar centrado en PLANT_CROWN_MID.
static void crownMask(const u8 *h, u8 *copa)
{
    u8 r;

    for (r = 0; r < PLANT_CROWN_ROWS; r++)
    {
        copa[r] = 0;
        if (h[r])
        {
            const u8 w = (u8) (h[r] - 1);
            u8 x;

            for (x = (u8) (PLANT_CROWN_MID - w); x <= (u8) (PLANT_CROWN_MID + w); x++)
                copa[r] |= (u8) (1u << x);
        }
    }
}

static bool crownInner(const u8 *copa, u8 x, u8 r)
{
    if (!(copa[r] & (u8) (1u << x)))
        return FALSE;
    if ((x == 0) || !(copa[r] & (u8) (1u << (x - 1))))
        return FALSE;
    if ((x >= 7) || !(copa[r] & (u8) (1u << (x + 1))))
        return FALSE;
    if ((r == 0) || !(copa[r - 1] & (u8) (1u << x)))
        return FALSE;
    if (((r + 1) >= PLANT_CROWN_ROWS) || !(copa[r + 1] & (u8) (1u << x)))
        return FALSE;

    return TRUE;
}

// Escribe en dst los 32 bytes de la tesela de la planta numero `shape`. Dos
// plantas del juego nunca comparten shape, y dos shapes nunca dan el mismo
// dibujo (lo comprueba el generador), asi que no hay dos plantas iguales.
// Las cuatro esquinas del spot, en un cuadro de 7x7. Cuadrado a proposito: con
// el mismo paso de 16 px en los dos ejes, el aire entre spots sale igual a los
// lados que arriba y abajo. Van debajo de la planta, que las tapa donde
// coincidan.
static void marcaDeSpot(u8 *px)
{
    px[(0 * 8) + 0] = PAL_PLANTA_SILUETA;
    px[(0 * 8) + 6] = PAL_PLANTA_SILUETA;
    px[(6 * 8) + 0] = PAL_PLANTA_SILUETA;
    px[(6 * 8) + 6] = PAL_PLANTA_SILUETA;
}

// Solo la marca: la casilla de la retícula que no le toca a ninguna planta.
void Plants_buildSpotTile(u8 *dst)
{
    u8 px[64];
    u8 x, y;

    for (x = 0; x < 64; x++)
        px[x] = PAL_SUELO;
    marcaDeSpot(px);

    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x += 2)
            *dst++ = (u8) ((px[(y * 8) + x] << 4) | px[(y * 8) + x + 1]);
}

static void buildTile(u16 shape, u8 *dst, bool silueta, bool marca)
{
    const u8 *const h = plantProfile[shape % PLANT_PROFILE_COUNT];
    const u8 fill = (u8) (shape % PLANT_FILL_COUNT);
    const u8 stem = (u8) (shape % PLANT_STEM_COUNT);
    u8 px[64];
    u8 copa[PLANT_CROWN_ROWS];
    u8 base = 0;
    u8 x, y, r;

    for (x = 0; x < 64; x++)
        px[x] = PAL_SUELO;
    if (marca)
        marcaDeSpot(px);

    crownMask(h, copa);
    for (r = 0; r < PLANT_CROWN_ROWS; r++)
        if (copa[r])
            base = r;

    for (r = 0; r <= base; r++)
    {
        for (x = 0; x < 8; x++)
        {
            bool dentro;
            u8 c;

            if (!(copa[r] & (u8) (1u << x)))
                continue;

            dentro = crownInner(copa, x, r);
            switch (fill)
            {
                case 0:  c = PAL_PLANTA_CLARA; break;
                case 1:  c = (r == base) ? PAL_PLANTA_MEDIA : PAL_PLANTA_CLARA; break;
                case 2:  c = dentro ? PAL_PLANTA_MEDIA : PAL_PLANTA_CLARA; break;
                case 3:  c = dentro ? PAL_SUELO : PAL_PLANTA_CLARA; break;
                case 4:  c = ((x + r) & 1) ? PAL_PLANTA_CLARA : PAL_PLANTA_MEDIA; break;
                default: c = !dentro ? PAL_PLANTA_CLARA
                                     : (((x + r) & 1) ? PAL_PLANTA_MEDIA : PAL_SUELO); break;
            }
            px[(r * 8) + x] = c;
        }
    }

    for (y = (u8) (base + 1); y < 7; y++)
    {
        px[(y * 8) + PLANT_CROWN_MID] = PAL_PLANTA_MEDIA;
        if (stem == 1)
        {
            px[(y * 8) + PLANT_CROWN_MID - 1] = PAL_PLANTA_MEDIA;
            px[(y * 8) + PLANT_CROWN_MID + 1] = PAL_PLANTA_MEDIA;
        }
        else if (stem == 3)
        {
            px[(y * 8) + PLANT_CROWN_MID] = PAL_SUELO;
            px[(y * 8) + PLANT_CROWN_MID - 1] = PAL_PLANTA_MEDIA;
            px[(y * 8) + PLANT_CROWN_MID + 1] = PAL_PLANTA_MEDIA;
        }
    }
    if (stem == 2)
    {
        const u8 hoja = (u8) (((base + 2) < 6) ? (base + 2) : 6);

        px[(hoja * 8) + PLANT_CROWN_MID - 2] = PAL_PLANTA_MEDIA;
        px[(hoja * 8) + PLANT_CROWN_MID - 1] = PAL_PLANTA_MEDIA;
        px[(hoja * 8) + PLANT_CROWN_MID + 1] = PAL_PLANTA_MEDIA;
        px[(hoja * 8) + PLANT_CROWN_MID + 2] = PAL_PLANTA_MEDIA;
    }
    else if (stem == 4)
    {
        px[((base + 1) * 8) + PLANT_CROWN_MID + 1] = PAL_PLANTA_MEDIA;
        px[((base + 1) * 8) + PLANT_CROWN_MID + 2] = PAL_PLANTA_MEDIA;
    }

    for (x = 0; x < 8; x++)
        if (plantRoot[stem] & (u8) (1u << x))
            px[(7 * 8) + x] = PAL_PLANTA_OSCURA;

    // La silueta es la misma planta en un tono propio, mas apagado que
    // cualquiera de los suyos: se ve la forma pero no compite con las que si
    // estan recogidas.
    if (silueta)
        for (x = 0; x < 64; x++)
            if (px[x] != PAL_SUELO)
                px[x] = PAL_PLANTA_SILUETA;

    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x += 2)
            *dst++ = (u8) ((px[(y * 8) + x] << 4) | px[(y * 8) + x + 1]);
}

// La planta a secas: es la que va en el laberinto, sin nada de la reticula.
void Plants_buildTile(u16 shape, u8 *dst)
{
    buildTile(shape, dst, FALSE, FALSE);
}

// La de la coleccion: lleva la marca del spot, y apagada mientras no se tenga.
void Plants_buildSpot(u16 shape, u8 *dst, bool recogida)
{
    buildTile(shape, dst, !recogida, TRUE);
}

// La silueta sola, sin marca: la del zoom de una planta que aun no se tiene.
void Plants_buildSilhouette(u16 shape, u8 *dst)
{
    buildTile(shape, dst, TRUE, FALSE);
}

// Rehace las PLANTS_PER_ROOM teselas de esta habitacion y las sube. Solo
// cuando cambia de habitacion: recoger una planta no mueve ninguna.
static void uploadRoomTiles(void)
{
    u8 i;

    if (curSlot == tilesSlot)
        return;

    for (i = 0; i < PLANTS_PER_ROOM; i++)
        Plants_buildTile((u16) ((curSlot * PLANTS_PER_ROOM) + i), (u8 *) &plantTileData[i * 8]);

    VDP_loadTileData(plantTileData, PLANT_TILE_BASE, PLANTS_PER_ROOM, DMA);
    tilesSlot = curSlot;
}

// Que plantas tiene de verdad esa habitacion, un bit por hueco igual que
// collectedMask. 0 si nunca se cartografio: entonces no se sabe y no se
// dibuja hueco ninguno.
u16 Plants_cachedRoomMask(u16 slot)
{
    u16 mask = 0;
    u8 line, i;

    if ((slot >= PLANTS_CACHE_SLOTS) || !cacheValid[slot])
        return 0;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
        for (i = 0; i < cacheCount[slot][line]; i++)
            mask |= (u16) (1u << ((line * PLANTS_MAX_PER_LINE) + i));

    return mask;
}

// El nombre de una planta. Sale de su propio indice con una tabla de silabas,
// asi que es estable y no se repite: multiplicar por un impar y quedarse con
// 12 bits es biyectivo, y hay 4096 nombres para 2565 plantas.
void Plants_name(u16 shape, char *out)
{
    static const char *const silA[16] = {
        "VEX", "KOR", "THAL", "ZUN", "MIR", "OLD", "BRAC", "SYL",
        "NEV", "QUIS", "DRA", "FEN", "UMB", "GLAU", "PYR", "ASH",
    };
    static const char *const silB[16] = {
        "ARIS", "ONE", "ULA", "ITH", "ORA", "ENSE", "YLL", "UMA",
        "ASCA", "IRIS", "OLIX", "ANTH", "ERNA", "USCA", "IDIA", "ORN",
    };
    static const char *const silC[16] = {
        "MOLT", "VERN", "CRIS", "PALE", "DUSK", "FERN", "GRIM", "WAX",
        "SILT", "TIDE", "MYRR", "COAL", "SNOW", "RUST", "VEIL", "BLOOM",
    };
    const u16 n = (u16) ((shape * 2053u) & 4095u);

    sprintf(out, "%s%s %s", silA[n & 15], silB[(n >> 4) & 15], silC[(n >> 8) & 15]);
}

u16 Plants_tilesEnd(void)
{
    return (u16) (PLANT_TILE_BASE + PLANTS_PER_ROOM);
}

void Plants_loadGraphics(void)
{
    PAL_setColor(PAL_PLANTA_OSCURA, RGB3_3_3_TO_VDPCOLOR(2, 0, 2));
    PAL_setColor(PAL_PLANTA_MEDIA,  RGB3_3_3_TO_VDPCOLOR(5, 2, 4));
    PAL_setColor(PAL_PLANTA_CLARA,  RGB3_3_3_TO_VDPCOLOR(7, 5, 6));
    PAL_setColor(PAL_PLANTA_SILUETA, RGB3_3_3_TO_VDPCOLOR(3, 1, 2));
}

void Plants_reset(void)
{
    u8 r, c, line;

    for (r = 0; r < MAX_MAP_ROWS; r++)
        for (c = 0; c < MAX_MAP_COLS; c++)
        {
            collectedMask[r][c] = 0;
        }

    // cacheValid is deliberately NOT cleared here any more. It used to be,
    // because the cache was indexed by map position alone and a different
    // planet put different rooms at those positions. Slots are per planet
    // now, and a planet's mapSeed is decided once per session, so what is
    // in there stays true for the rest of the session -- which is the
    // whole point: it is filled during the cartography screen at boot, so
    // no room has to pay for its own first visit mid-play.

    tilesSlot = 0xFFFF;
    total = 0;
    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
        curCount[line] = 0;
}

void Plants_spawnForRoom(u16 roomSeed, u16 slot)
{
    const u16 nodeCount = Maze_slideNodeCount();
    u8 occCol[PLANTS_LINES_PER_ROOM * PLANTS_MAX_PER_LINE];
    u8 occRow[PLANTS_LINES_PER_ROOM * PLANTS_MAX_PER_LINE];
    u8 occCount = 0;
    u8 line;

    curSlot = (slot <= PLANTS_CACHE_SLOTS) ? slot : PLANTS_CACHE_SLOTS;
    seedRng(roomSeed);

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
        curCount[line] = 0;

    // nodeCount == 0: no slide-graph guarantee for this room (the rare
    // carve fallback -- maze.h's own doc comment says it's never actually
    // been observed to trigger -- or the insertion room, which never
    // builds one). No plants there rather than an unverifiable guess.
    if (nodeCount == 0)
        return;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        u8 attempt;

        for (attempt = 0; (attempt < PLANTS_SPAWN_ATTEMPTS) && (curCount[line] == 0); attempt++)
        {
            const u16 node = (u16) (rngNext() % nodeCount);
            const u8 dir = (u8) (rngNext() % 4);
            const s16 target = Maze_slideEdge(node, dir);
            s16 srcX, srcY, dstX, dstY;
            // static for the same reason tomboTooEasy's queues are: these
            // doubled when a cell became 8px (PLANTS_MAX_EDGE_LEN went
            // from 20 to 40), and this runs deep inside room loading.
            static s16 px[PLANTS_MAX_EDGE_LEN], py[PLANTS_MAX_EDGE_LEN];
            u8 pathLen = 0;
            bool hitHub = FALSE;
            s16 x, y;

            if (target < 0)
                continue; // that slide runs into a wall right away, or straight out a door -- no edge there

            Maze_slideNodePos(node, &srcX, &srcY);
            Maze_slideNodePos((u16) target, &dstX, &dstY);

            // Walks every cell strictly between the two nodes, same straight
            // run the ship's own slide crosses making this exact move (spec
            // §48, user request: "las lineas de plantas tienen que estar en
            // una linea del grafo accesible para la nave") -- these two nodes
            // are both real, reachable stops (tomboValidate/tomboRebuildGraph
            // already guarantee that), so every cell strictly between them,
            // on the one straight line connecting them, is unavoidably
            // crossed by that slide too.
            //
            // BUG FIX (found by host fuzzing with locked doors): the slide
            // graph is always built as if every door in doorMask were
            // unlocked (maze.h's own doc comment on Maze_generateRoom's
            // lockedN/E/S/W: "the validator always treats doors as
            // unlocked; sealing only closes the border"), and that sealing
            // -- overwriting a locked door's threshold cells with a wall
            // tile -- happens AFTER the graph is built, so a node/edge can
            // sit exactly on what is, by the time this function runs, really
            // a wall. Maze_isWall() below (checked on the FINAL, already-
            // sealed grid) is the actual source of truth; the graph only
            // picks realistic CANDIDATE lines to try, same spirit as
            // enemy.c's own findOpenInLane re-checking Maze_isWall per cell
            // instead of trusting a precomputed lane blindly. isOccupied()
            // stops it the same way at a cell an EARLIER line (this same
            // room, this same spawn) already claimed, so the
            // PLANTS_LINES_PER_ROOM lines never overlap each other.
            x = (s16) (srcX + dirDX[dir]);
            y = (s16) (srcY + dirDY[dir]);
            // Maze_isPlantSafe (maze.h): the cell must be crossable from
            // EVERY door, not just reachable on the graph -- the graph is
            // the union over all entries and the player only gets one, so
            // without this a line can land where the ship can never follow
            // and the room can never be cleared.
            while ((pathLen < PLANTS_MAX_EDGE_LEN) && !Maze_isWall(x, y) &&
                   Maze_isPlantSafe(x, y) && !isOccupied(x, y, occCol, occRow, occCount))
            {
                px[pathLen] = x;
                py[pathLen] = y;
                if ((x == MAZE_DOOR_COL) && (y == MAZE_DOOR_ROW))
                    hitHub = TRUE; // don't overlap the item letter's own fixed spot
                pathLen++;

                if ((x == dstX) && (y == dstY))
                    break;

                x = (s16) (x + dirDX[dir]);
                y = (s16) (y + dirDY[dir]);
            }

            if (hitHub || (pathLen < PLANTS_LINE_MIN))
                continue;

            {
                const u8 cap = (pathLen < PLANTS_MAX_PER_LINE) ? pathLen : PLANTS_MAX_PER_LINE;
                const u8 len = (u8) (PLANTS_LINE_MIN + (rngNext() % ((cap - PLANTS_LINE_MIN) + 1)));
                const u8 startAt = (u8) (rngNext() % (pathLen - len + 1));
                u8 i;

                for (i = 0; i < len; i++)
                {
                    curCol[line][i] = (u8) px[startAt + i];
                    curRow[line][i] = (u8) py[startAt + i];
                    occCol[occCount] = (u8) px[startAt + i];
                    occRow[occCount] = (u8) py[startAt + i];
                    occCount++;
                }
                curCount[line] = len;
            }
        }
    }
}

bool Plants_tryCollect(u8 col, u8 row, s16 playerX, s16 playerY)
{
    bool any = FALSE;
    u8 line, i;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        for (i = 0; i < curCount[line]; i++)
        {
            const u8 bit = (u8) ((line * PLANTS_MAX_PER_LINE) + i);

            if (collectedMask[row][col] & (u16) (1u << bit))
                continue;

            {
                const s16 px = curCol[line][i] * MAZE_TILE_PX;
                const s16 py = curRow[line][i] * MAZE_TILE_PX;
                // The plant is ONE text glyph, 8x8, sitting in the top-left
                // quarter of its 16x16 cell (Plants_drawInRoom) -- so that
                // 8x8 is its collision box, not the whole cell. Testing the
                // whole cell made it vanish while the ship was still a
                // glyph's width away on the right or below it, which is
                // exactly the "colisiona un poco abajo derecha" the player
                // reported. The ship's own box stays the full 16.
                const bool overlap = (playerX < px + PLANTS_GLYPH_PX) && (px < playerX + MAZE_TILE_PX) &&
                                      (playerY < py + PLANTS_GLYPH_PX) && (py < playerY + MAZE_TILE_PX);

                if (overlap)
                {
                    collectedMask[row][col] |= (u16) (1u << bit);
                    total++;
                    any = TRUE;
                }
            }
        }
    }

    return any;
}

void Plants_drawInRoom(u8 col, u8 row)
{
    u8 line, i;

    uploadRoomTiles();

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        for (i = 0; i < curCount[line]; i++)
        {
            const u8 bit = (u8) ((line * PLANTS_MAX_PER_LINE) + i);

            if (collectedMask[row][col] & (u16) (1u << bit))
                continue;

            // El cuarto de arriba a la izquierda de la celda, misma esquina que
            // la caja de recogida y que la letra de items.c.
            VDP_setTileMapXY(BG_A,
                             TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE,
                                            (u16) (PLANT_TILE_BASE + (line * PLANTS_MAX_PER_LINE) + i)),
                             Maze_cellColumn(curCol[line][i]),
                             Maze_cellRow(curRow[line][i]));
        }
    }
}

void Plants_drawHud(void)
{
    char buf[20];

    sprintf(buf, "PLANTS:%d/%d", total, planetTotal);
    Hud_drawText(buf, 14, 1); // fuente propia del HUD, igual que Items_drawHud
}

u16 Plants_collectedCount(void)
{
    return total;
}

void Plants_setCollected(u16 count)
{
    total = count;
}

void Plants_saveMask(u16 outMask[MAX_MAP_ROWS][MAX_MAP_COLS])
{
    u8 r, c;

    for (r = 0; r < MAX_MAP_ROWS; r++)
        for (c = 0; c < MAX_MAP_COLS; c++)
            outMask[r][c] = collectedMask[r][c];
}

void Plants_restoreMask(const u16 inMask[MAX_MAP_ROWS][MAX_MAP_COLS])
{
    u8 r, c;

    for (r = 0; r < MAX_MAP_ROWS; r++)
        for (c = 0; c < MAX_MAP_COLS; c++)
            collectedMask[r][c] = inMask[r][c];
}

void Plants_setPlanetTotal(u16 t)
{
    planetTotal = t;
}

void Plants_cacheCurrentRoom(u16 slot)
{
    u8 line, i;

    if (slot >= PLANTS_CACHE_SLOTS)
        return; // not a real room, or the table outgrew the cache

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        cacheCount[slot][line] = curCount[line];
        for (i = 0; i < curCount[line]; i++)
        {
            cacheCol[slot][line][i] = curCol[line][i];
            cacheRow[slot][line][i] = curRow[line][i];
        }
    }

    cacheValid[slot] = TRUE;
}

bool Plants_loadCachedRoom(u16 slot)
{
    u8 line, i;

    if ((slot >= PLANTS_CACHE_SLOTS) || !cacheValid[slot])
        return FALSE;

    curSlot = slot;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        curCount[line] = cacheCount[slot][line];
        for (i = 0; i < curCount[line]; i++)
        {
            curCol[line][i] = cacheCol[slot][line][i];
            curRow[line][i] = cacheRow[slot][line][i];
        }
    }

    return TRUE;
}

u16 Plants_lastRoomCount(void)
{
    u16 sum = 0;
    u8 line;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
        sum += curCount[line];

    return sum;
}

bool Plants_cellOccupied(s16 x, s16 y)
{
    u8 line, i;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
        for (i = 0; i < curCount[line]; i++)
            if ((curCol[line][i] == x) && (curRow[line][i] == y))
                return TRUE;

    return FALSE;
}

bool Plants_uncollectedAt(u8 roomCol, u8 roomRow, s16 x, s16 y)
{
    u8 line, i;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        for (i = 0; i < curCount[line]; i++)
        {
            const u8 bit = (u8) ((line * PLANTS_MAX_PER_LINE) + i);

            if ((curCol[line][i] == x) && (curRow[line][i] == y) &&
                !(collectedMask[roomRow][roomCol] & (u16) (1u << bit)))
                return TRUE;
        }
    }

    return FALSE;
}

bool Plants_allCollectedInRoom(u8 col, u8 row)
{
    u8 line, i;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        for (i = 0; i < curCount[line]; i++)
        {
            const u8 bit = (u8) ((line * PLANTS_MAX_PER_LINE) + i);

            if (!(collectedMask[row][col] & (u16) (1u << bit)))
                return FALSE;
        }
    }

    return TRUE;
}
