#include "plants.h"
#include "maze.h"
#include "guidemap.h" // MAX_MAP_COLS/MAX_MAP_ROWS

// 3 independent lines per room (user request: "añade dos hileras más de
// plantas... en cada room" -- 2 more on top of the original 1). Also the
// width of collectedMask's per-room bitmask: PLANTS_LINES_PER_ROOM *
// PLANTS_MAX_PER_LINE bits must fit in collectedMask's u16 (15 of 16 used).
#define PLANTS_LINES_PER_ROOM 3
#define PLANTS_MAX_PER_LINE   5
#define PLANTS_LINE_MIN       3
// Random (anchor,axis,dir) tries before giving up on ONE line -- tombo
// rooms are ~90% open interior (maze.h's own doc comment), so this
// converges fast in practice, same reasoning as enemy.c's findOpenInLane.
#define PLANTS_SPAWN_ATTEMPTS 24

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
static u8 cacheCol[MAX_MAP_ROWS][MAX_MAP_COLS][PLANTS_LINES_PER_ROOM][PLANTS_MAX_PER_LINE];
static u8 cacheRow[MAX_MAP_ROWS][MAX_MAP_COLS][PLANTS_LINES_PER_ROOM][PLANTS_MAX_PER_LINE];
static u8 cacheCount[MAX_MAP_ROWS][MAX_MAP_COLS][PLANTS_LINES_PER_ROOM];
static bool cacheValid[MAX_MAP_ROWS][MAX_MAP_COLS];
static u16 total;

// This planet's real plant count (Plants_setPlanetTotal) -- the
// denominator Plants_drawHud() prints, separate from `total` above
// (confusingly also named "total", but that one is the COLLECTED count).
// A per-planet constant, not session state, so Plants_reset() leaves it
// alone.
static u16 planetTotal;

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
// cross in either axis (the larger of MAZE_W/MAZE_H).
#define PLANTS_MAX_EDGE_LEN 20

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

void Plants_reset(void)
{
    u8 r, c, line;

    for (r = 0; r < MAX_MAP_ROWS; r++)
        for (c = 0; c < MAX_MAP_COLS; c++)
        {
            collectedMask[r][c] = 0;
            cacheValid[r][c] = FALSE; // a different mapSeed puts different rooms at these positions
        }

    total = 0;
    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
        curCount[line] = 0;
}

void Plants_spawnForRoom(u16 roomSeed)
{
    const u16 nodeCount = Maze_slideNodeCount();
    u8 occCol[PLANTS_LINES_PER_ROOM * PLANTS_MAX_PER_LINE];
    u8 occRow[PLANTS_LINES_PER_ROOM * PLANTS_MAX_PER_LINE];
    u8 occCount = 0;
    u8 line;

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
            s16 px[PLANTS_MAX_EDGE_LEN], py[PLANTS_MAX_EDGE_LEN];
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
                const bool overlap = (playerX < px + MAZE_TILE_PX) && (px < playerX + MAZE_TILE_PX) &&
                                      (playerY < py + MAZE_TILE_PX) && (py < playerY + MAZE_TILE_PX);

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

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        for (i = 0; i < curCount[line]; i++)
        {
            const u8 bit = (u8) ((line * PLANTS_MAX_PER_LINE) + i);

            if (collectedMask[row][col] & (u16) (1u << bit))
                continue;

            // maze cell -> 2x2 VDP tiles, same convention as items.c
            VDP_drawText("*", curCol[line][i] * 2, (curRow[line][i] * 2) + MAZE_ORIGIN_ROW);
        }
    }
}

void Plants_drawHud(void)
{
    char buf[20];

    sprintf(buf, "PLANTAS:%d/%d", total, planetTotal);
    VDP_setTextPriority(1); // draw above BG_A's low-priority maze tiles, same as Items_drawHud
    VDP_drawTextBG(BG_B, buf, 14, 1);
    VDP_setTextPriority(0);
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

void Plants_cacheCurrentRoom(u8 col, u8 row)
{
    u8 line, i;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        cacheCount[row][col][line] = curCount[line];
        for (i = 0; i < curCount[line]; i++)
        {
            cacheCol[row][col][line][i] = curCol[line][i];
            cacheRow[row][col][line][i] = curRow[line][i];
        }
    }

    cacheValid[row][col] = TRUE;
}

bool Plants_loadCachedRoom(u8 col, u8 row)
{
    u8 line, i;

    if (!cacheValid[row][col])
        return FALSE;

    for (line = 0; line < PLANTS_LINES_PER_ROOM; line++)
    {
        curCount[line] = cacheCount[row][col][line];
        for (i = 0; i < curCount[line]; i++)
        {
            curCol[line][i] = cacheCol[row][col][line][i];
            curRow[line][i] = cacheRow[row][col][line][i];
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
