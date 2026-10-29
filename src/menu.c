#include "menu.h"
#include "resources.h"
#include "player.h"
#include "maze.h"     // MAZE_TILE_COUNT / MAZE_SCRATCH_TILE_COUNT, for the title tiles
#include "planetart.h" // el dibujo de los planetas, que cambia con la galaxia
#include "guidemap.h" // GUIDEMAP_TILE_COUNT, same

// 320x224 screen. Sun sits left of dead-center vertically so the
// biggest orbit's top edge (with room for the cursor arrow above
// whichever planet sits there) stays clear of the title text, and its
// bottom edge stays clear of the size-name/hint text below (main.c's
// drawMenu()) -- main.c's drawMenu() moved its title up and its bottom
// text down (spec §32quat) to free up the extra vertical room these
// wider orbits need.
#define SUN_CENTER_X 160
// Subido 8 px respecto a los 142 que tenia: la orbita mas externa llega a
// +34 y el planeta mas grande mide 12 de radio, o sea que antes bajaba
// hasta la fila 23 y dejaba el bloque de texto apelmazado en 24..27. Con
// 134 acaba en la 22 y el texto cabe en 23/25/27, con aire entre lineas.
#define SUN_CENTER_Y 134

// menuSun is a real 32x32 (4x4 tile) sprite, not a BG-tile fill (spec
// §32septies, user request: "por que el bloque del centro no es un
// círculo?") -- a BG_A tile fill can only test whole 8x8 tiles against
// the sun's radius, and at the size the sun needs to be that produced a
// plain blocky rectangle, not a circle (verified by hand: with the old
// SUN_RADIUS_PX=12, exactly the 2x3 block of tiles centered on the sun
// passed the per-tile radius test, no partial/corner tiles at all).
// menu_sun.png bakes a real per-pixel circular mask instead, same
// technique the planet sprites already use successfully at this scale.
// Its own raster scan order is dark-first/violet-second, matching
// EVERY other sprite in this game (playerShip, enemyShip, mapShip, the
// planets, the cursor) -- so it renders using PAL1 (playerShip/mapShip)
// with zero extra palette work needed for its own compiled pixel data.
#define SUN_HALF_PX 16
// Absolute CRAM index of PAL1's index1 -- the same slot main.c's
// PLAYER_SHIP_INK_INDEX flips between violet and white for the map-ship
// blink (spec §23). Reused here as a temporary override: playerShip and
// mapShip are both hidden for the entire time the menu is shown (main.c
// hides them at boot and in resetToMenu()), so nothing else depends on
// this slot's color while the sun sprite needs it to read white instead
// of its default violet. Menu_setVisible flips it to white on entry and
// restores playerShip's own violet on exit, mirroring the exact
// override/restore pattern main.c's blink code already uses on this
// same slot.
#define SUN_INK_INDEX ((PAL1 * 16) + 1)

// Absolute CRAM index of PAL2's index1 (spec §45, user request: "Los
// planetas que se han completados pintalos de amarillo") -- PAL2 is
// enemyShip's palette, otherwise completely unused for the entire time
// the menu is shown: enemySprites are always hidden then (main.c hides
// them at boot, in resetToMenu(), and when a run's own victory screen
// starts), exactly like PAL1/SUN_INK_INDEX's own reasoning above. A
// completed planet's sprite is pointed at PAL2 instead of PAL3 (see
// Menu_update's SPR_setPalette calls) -- same 2-color dark+violet pixel
// data every planet sprite already has, just reading its violet pixel
// from a CRAM slot this file pokes to yellow instead. Menu_setVisible
// flips it to yellow on entry and restores enemyShip's own violet on
// exit, mirroring SUN_INK_INDEX's override/restore pattern exactly.
#define COMPLETED_INK_INDEX ((PAL2 * 16) + 1)

// Absolute CRAM indices of PAL3's index0/1 (user request: "marca las
// puertas cerradas de color amarillo") -- PAL3 is THIS file's own planet/
// cursor palette, so the roles are reversed from SUN_INK_INDEX/
// COMPLETED_INK_INDEX above: PAL3 is busy (planetSmall's own baked colors)
// for exactly as long as the menu is visible, and completely free the rest
// of the time, gameplay included (see Menu_loadGraphics's own doc comment)
// -- the opposite lifetime maze.c's locked-door tiles need. Maze_draw()
// points a sealed door's tile at PAL3 instead of the room's own hue
// (same dither shape, just a different palette), reading index0 as the
// maze's usual dark background and index1 as yellow. Menu_setVisible
// borrows both slots for that while the menu is HIDDEN, and puts
// planetSmall's own baked values back the instant the menu becomes
// visible again -- same override/restore shape as SUN_INK_INDEX/
// COMPLETED_INK_INDEX, just swapped which state does the overriding.
#define LOCKED_DOOR_BG_INDEX  ((PAL3 * 16) + 0)
#define LOCKED_DOOR_INK_INDEX ((PAL3 * 16) + 1)

// Absolute CRAM index of PAL0's index1, borrowed for the grey of a planet
// that hasn't been unlocked yet (user request: "enseña los planetas
// bloqueados pero en gris"). Same borrow-and-restore shape as the three
// overrides above, and PAL0's index1 is free for exactly as long as the
// menu is up: it is maze.c's wall colour, and no maze is on screen then.
// Not the text -- SGDK's own font is drawn in colour index 15, which is
// why the menu's lettering doesn't go grey along with the planets.
#define LOCKED_PLANET_INK_INDEX ((PAL0 * 16) + 1)
#define LOCKED_PLANET_COLOR     0x6D6D6D
#define MAZE_WALL_COLOR         0x987DFA // maze.c's own WALL_COLOR_RGB, put back on the way out


// Elliptical, not circular (spec §31) -- makes better use of the
// 320x224 screen's aspect ratio than a true circle would. Radii grow
// with MENU_PLANET_COUNT so every orbit reads as a clearly nested ring,
// same order as main.c's sizePresets[] (smallest/fewest-letter map =
// innermost, spec §33). 8 rings now (spec §33's 7, plus the 1x1 tombo
// test planet added ahead of all of them as the new innermost ring,
// user request) -- recomputed from scratch with the same two fixed
// endpoints spec §33 used for its own 7-ring recompute, just
// re-interpolated for one more ring:
// - Outer ring (index 7, the old largest/10x8 preset) keeps the exact
//   same hard ceiling as before -- it's derived from the screen/sprite
//   sizes, not from the ring count: X <= screen half-width minus the
//   largest planet sprite's half-width and a small margin
//   (160-12-4=144); Y <= the gap between the title and bottom text
//   minus the cursor's own clearance above the biggest planet (62).
// - Inner ring (index 0, the new 1x1 test preset) has to clear the SUN
//   sprite (menuSun, ~13px visual radius) plus its own planet
//   half-width (4) plus a few px of gap, on the tighter axis (Y, since
//   the ellipse is wider than tall): 13+4+3=20 is the floor used here
//   for Y -- same floor as spec §33's own old index-0 ring, since the
//   new test planet reuses the smallest sprite size too; X uses the
//   same X/Y aspect ratio as the outer ring (144/62≈2.32) so every ring
//   reads as a consistent ellipse: 20*2.32≈46.
// - The other 6 rings are linearly interpolated between those two fixed
//   endpoints on each axis independently -- 98/7=14 per ring on X,
//   42/7=6 per ring on Y, both clean steps.
// Still used to ANIMATE each planet's position (Menu_update below) even
// though the orbit lines themselves are no longer drawn (spec §32sexies,
// user request) -- the planets still travel these exact elliptical
// paths, just without a visible line traced under them.
static const s16 orbitRadiusX[MENU_PLANET_COUNT] = { 46, 60, 74, 88, 102, 116, 130, 144 };
static const s16 orbitRadiusY[MENU_PLANET_COUNT] = { 11, 15, 18, 21, 24, 28, 31, 34 };

// Per-planet angular speed (degrees/frame at 60fps) and starting angle
// (spread apart so the planets don't all launch aligned) -- purely
// decorative, no gameplay meaning. Inner planet orbits faster, like a
// real solar system. 8 planets now: linearly interpolated from 1.8
// (innermost, the new test planet) down to 0.5 (outermost), rounded to
// 1 decimal place, same endpoints spec §33's own 7-value table used.
static const fix16 orbitSpeed[MENU_PLANET_COUNT] = {
    FIX16(1.8), FIX16(1.6), FIX16(1.4), FIX16(1.2), FIX16(1.1), FIX16(0.9), FIX16(0.7), FIX16(0.5)
};
static fix16 orbitAngle[MENU_PLANET_COUNT];

// Sprite half-width/half-height in pixels, matching planetSmall (8x8),
// planetMedium (16x16), planetLarge (24x24) -- needed both to convert a
// computed CENTER position into SPR_setPosition's top-left corner, and
// to know how far above each planet's own edge the cursor should clear
// (spec §31: "justo encima del planeta"). Only 3 distinct sizes exist
// on real hardware (1/2/3 tiles/side -- 4 tiles/side is reserved for
// the sun, spec §32septies), so with 8 planets each size is reused
// across a small group of adjacent (by orbit radius) tiers: small for
// the 4 tiniest presets (including the new test planet), medium for the
// next 2, large for the original 2 biggest -- orbit radius is what
// actually communicates the 8-way progression, sprite size is a coarser
// secondary cue.
static const s16 planetHalfSize[MENU_PLANET_COUNT] = { 4, 4, 4, 4, 8, 8, 12, 12 };
#define CURSOR_GAP_PX 4  // visible gap between the cursor arrow and the planet's own top edge
#define CURSOR_HALF_W 4  // cursorArrow is 8x8
#define CURSOR_HALF_H 4

static Sprite *planetSprites[MENU_PLANET_COUNT];
// EL BARRIDO AL CAMBIAR DE GALAXIA (peticion del usuario: "cuando se
// seleccione galaxia, todos los planetas se muevan hacia arriba/abajo al mismo
// tiempo y desaparezcan"). Se suma a la Y de todo lo que orbita, asi que con
// una sola variable se van los ocho a la vez y siguen girando mientras se van.
#define SWEEP_FRAMES 10
#define SWEEP_DIST   140   // de sobra para sacarlos de la zona de juego
static s16 sweepOffsetY;

static Sprite *cursorSprite;

// Ping-pong suavizado del cursor: un paso cada 3 tramas, ciclo de 48.
static const s8 cursorBob[16] = { 0, 1, 1, 2, 2, 2, 1, 1, 0, -1, -1, -2, -2, -2, -1, -1 };
static u16 cursorBobTick;
static Sprite *sunSprite;

// QUIEN TAPA A QUIEN. En esta maquina el orden de la lista de sprites es el
// orden de dibujo, y el primero es el que queda ENCIMA; SPR_addSprite mete
// cada nuevo al final, asi que el sol, que se crea el ultimo, caia debajo de
// todo y los planetas le pasaban por delante incluso en la mitad de la orbita
// que va por detras de el.
//
// Con profundidad explicita hay tres capas: el cursor arriba del todo, el sol
// en medio, y cada planeta por delante o por detras del sol segun por donde
// vaya. La mitad de arriba de la elipse es la de atras.
#define DEPTH_CURSOR   (-2)
#define DEPTH_DELANTE  (-1)
#define DEPTH_SOL        0
#define DEPTH_DETRAS     1
// Solo se llama a SPR_setDepth cuando cambia de capa -- el sprite se reordena
// en el acto cada vez que se toca, y eso son dos veces por vuelta y no sesenta
// por segundo.
static s8 planetDepth[MENU_PLANET_COUNT];

// The big OVNI (user request: "solo OVNI en su lugar, alineado a la
// derecha y si puedes hacer la fuente mas grande para esa palabra pues
// mas grande"). SGDK's own font is 8x8 and can't be scaled, so these four
// letters are drawn here at 16x16 and turned into tiles at boot -- kept
// as the picture itself rather than as hex so they can be read, and
// edited, as what they are.
#define TITLE_LETTERS 4
#define TITLE_TILES   (TITLE_LETTERS * 4) // 2x2 tiles each
// Straight after everything maze.c and guidemap.c claim, the same way
// guidemap.c stacks onto maze.c's.
#define TITLE_TILE_BASE (TILE_USER_INDEX + MAZE_TILE_COUNT + GUIDEMAP_TILE_COUNT + MAZE_SCRATCH_TILE_COUNT)
// Top left (user request, correcting an earlier "a la derecha"), one tile
// in from the edge.
#define TITLE_X 1
#define TITLE_Y 1

static const char *const titleArt[TITLE_LETTERS][16] = {
    { // O
        "................",
        "...##########...",
        "..############..",
        "..###......###..",
        "..###......###..",
        "..###......###..",
        "..###......###..",
        "..###......###..",
        "..###......###..",
        "..###......###..",
        "..###......###..",
        "..############..",
        "...##########...",
        "................",
        "................",
        "................",
    },
    { // V
        "................",
        "###..........###",
        ".###........###.",
        ".###........###.",
        "..###......###..",
        "..###......###..",
        "...###....###...",
        "...###....###...",
        "....###..###....",
        "....###..###....",
        ".....######.....",
        ".....######.....",
        "......####......",
        ".......##.......",
        "................",
        "................",
    },
    { // N
        "................",
        "###..........###",
        "####.........###",
        "#####........###",
        "######.......###",
        "###.###......###",
        "###..###.....###",
        "###...###....###",
        "###....###...###",
        "###.....###..###",
        "###......###.###",
        "###.......######",
        "###........#####",
        "###.........####",
        "###..........###",
        "................",
    },
    { // I
        "................",
        "...##########...",
        "...##########...",
        "......####......",
        "......####......",
        "......####......",
        "......####......",
        "......####......",
        "......####......",
        "......####......",
        "......####......",
        "...##########...",
        "...##########...",
        "................",
        "................",
        "................",
    },
};

// Turns titleArt into real 4bpp tiles. Ink is colour index 15, the one
// SGDK's font uses too, so the word reads in exactly the same white as
// the rest of the menu's text with no palette of its own.
static void loadTitleTiles(void)
{
    u32 tile[8];
    u8 letter, quad, row, col;

    for (letter = 0; letter < TITLE_LETTERS; letter++)
    {
        for (quad = 0; quad < 4; quad++) // 0=TL 1=TR 2=BL 3=BR
        {
            const u8 ox = (u8) ((quad & 1) * 8);
            const u8 oy = (u8) ((quad >> 1) * 8);

            for (row = 0; row < 8; row++)
            {
                u32 bits = 0;

                for (col = 0; col < 8; col++)
                    bits = (bits << 4) | ((titleArt[letter][oy + row][ox + col] == '#') ? 0xFu : 0x0u);

                tile[row] = bits;
            }

            VDP_loadTileData(tile, (u16) (TITLE_TILE_BASE + (letter * 4) + quad), 1, CPU);
        }
    }
}

static void titlePutTile(u16 tileIndex, u16 x, u16 y)
{
    VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, tileIndex), x, y);
}

void Menu_drawTitle(void)
{
    u8 letter;

    for (letter = 0; letter < TITLE_LETTERS; letter++)
    {
        const u16 tx = (u16) (TITLE_X + (letter * 2));
        const u16 base = (u16) (TITLE_TILE_BASE + (letter * 4));

        titlePutTile((u16) (base + 0), tx,             TITLE_Y);
        titlePutTile((u16) (base + 1), (u16) (tx + 1), TITLE_Y);
        titlePutTile((u16) (base + 2), tx,             (u16) (TITLE_Y + 1));
        titlePutTile((u16) (base + 3), (u16) (tx + 1), (u16) (TITLE_Y + 1));
    }
}

// --- fila de recuadros, uno por planeta ---
#define BAR_X        4   // 8*4 = 32 tiles centrados: 4 de margen a cada lado
#define BAR_Y        5   // separado del titulo, con la galaxia en medio
#define BAR_BOX_W    4
#define BAR_BOX_H    4
#define BAR_GAP      0
#define BAR_STRIDE   (BAR_BOX_W + BAR_GAP)
#define BAR_INSET    2   // el marco se retranquea: deja 4 px de aire entre recuadros
#define BAR_NAME_Y   (BAR_Y + BAR_BOX_H)
#define BAR_NAME_LEN 4   // todos los nombres son de 4 letras
#define BAR_GALAXY_Y (BAR_Y - 1)

#define BAR_TILE_BASE   (TITLE_TILE_BASE + TITLE_TILES)
#define BAR_FRAME_SET   8                      // piezas por marco
// 48 de marco: 3 tintas x 2 grosores x 8 piezas. Los 36 que habia detras eran
// los circulos genericos del hueco de cada celda, y ya no estan: ahora el
// hueco lo llena el planeta de verdad (CELL_TILE_BASE).
#define BAR_TILES       48

// Cuatro letras por planeta, mismo orden que sizePresets[]. Ya no son
// constantes: main.c los sortea de nuevo en cada vuelta al menu (peticion del
// usuario: "que los nombres y los sonidos sean siempre aleatorios para cada
// partida"). Estos valores solo se verian si algo dibujase el menu antes del
// primer sorteo, igual que con galaxyName.
// EL BANCO DE LOS PLANETAS. Ya no salen de planet_small/medium/large.png:
// se dibujan en la consola cada vez que se cambia de galaxia (planetart.c), y
// sus teselas viven aqui, en VRAM que gestionamos nosotros. Los sprites se
// crean sin reserva ni subida automatica y se les dice que miren a este banco.
//
// 4 planetas de 8x8 (1 tesela), 2 de 16x16 (4) y 2 de 24x24 (9) = 30.
#define PLANET_TILE_BASE (BAR_TILE_BASE + BAR_TILES)
#define PLANET_ART_TILES 30

// EL MISMO PLANETA EN EL SELECTOR (peticion del usuario: "esos planetas, los
// mismos que aparecen en la animacion, figuren tambien en el selector"). El
// hueco de una celda son 2x2 teselas, asi que aqui van los ocho dibujados a
// 16x16 -- el mismo planeta, al tamano de la celda. Son teselas de FONDO, asi
// que van en orden de FILAS y no de columnas como las de sprite.
#define CELL_TILE_BASE (PLANET_TILE_BASE + PLANET_ART_TILES)
#define CELL_SIDE      16
#define CELL_TILES     (MENU_PLANET_COUNT * 4)

_Static_assert(TITLE_TILES + BAR_TILES + PLANET_ART_TILES + CELL_TILES == MENU_TILE_COUNT,
              "MENU_TILE_COUNT tiene que cubrir titulo, barra, planetas y celdas");

static const u8 planetSide[MENU_PLANET_COUNT] = { 8, 8, 8, 8, 16, 16, 24, 24 };
static const u8 planetTileAt[MENU_PLANET_COUNT] = { 0, 1, 2, 3, 4, 8, 12, 21 };

static char planetNames[MENU_PLANET_COUNT][BAR_NAME_LEN + 1] = {
    "XENU", "ZARG", "QORG", "NYXX", "VOID", "KLOP", "WURM", "ZYGN"
};

const char *Menu_planetName(u8 index)
{
    return planetNames[(index < MENU_PLANET_COUNT) ? index : 0];
}

// Cuatro letras exactas: la barra coloca cada nombre debajo de SU recuadro con
// BAR_STRIDE de paso, asi que uno mas largo se metaria en el de al lado.
void Menu_setPlanetName(u8 index, const char *name)
{
    u8 i;

    if (index >= MENU_PLANET_COUNT)
        return;
    for (i = 0; (i < BAR_NAME_LEN) && name[i]; i++)
        planetNames[index][i] = name[i];
    while (i < BAR_NAME_LEN)
        planetNames[index][i++] = ' ';
    planetNames[index][BAR_NAME_LEN] = '\0';
}

// Lo rellena main.c una vez por sesion, en cuanto random() esta sembrado
// de verdad (spec 54: no lo esta hasta la primera pulsacion real). Las dos
// pantallas que lo muestran -- esta y la de cartografia -- leen de aqui,
// asi que coinciden por construccion. El valor inicial solo se veria si
// algo dibujase el menu antes de esa primera pulsacion, cosa que no pasa.
static char galaxyName[20] = "GALAXY ---";

const char *Menu_galaxyName(void)
{
    return galaxyName;
}

void Menu_setGalaxyName(const char *name)
{
    u8 i = 0;

    while (name[i] && (i < (u8) (sizeof(galaxyName) - 1)))
    {
        galaxyName[i] = name[i];
        i++;
    }
    galaxyName[i] = '\0';
}

// Indice 2 = el violeta de los planetas, copiado desde PAL3 por
// Menu_setVisible; indice 1 = el gris de los bloqueados.
#define BAR_INK_INDEX ((PAL0 * 16) + 2)   // the planets' violet
// 0 = blanco, 1 = gris (bloqueado), 2 = violeta (solo el contorno del seleccionado)
static const u8 circleInk[3] = { 15, 1, 2 };
static u16 savedBarInk;
static bool barInkSaved;

// El nombre de cada planeta se pinta con la fuente leyendo el indice 15 de
// una paleta u otra: gris si esta bloqueado, violeta si esta abierto,
// amarillo si esta superado. Mismos tres colores que el propio planeta.
#define NAME_PAL_LOCKED    PAL1
#define NAME_PAL_OPEN      PAL3
#define NAME_PAL_DONE      PAL2
static u16 savedNameInk[3];

static void barPackTile(const u8 px[8][8], u16 index)
{
    u32 tile[8];
    u8 row, col;

    for (row = 0; row < 8; row++)
    {
        u32 bits = 0;
        for (col = 0; col < 8; col++)
            bits = (bits << 4) | px[row][col];
        tile[row] = bits;
    }
    VDP_loadTileData(tile, index, 1, CPU);
}

static void loadBarTiles(void)
{
    u8 px[8][8];
    u8 piece, row, col, grosor, variante, ink;

    for (ink = 0; ink < 3; ink++)
      for (variante = 0; variante < 2; variante++)
      {
        grosor = (u8) (variante ? 2 : 1);   // el seleccionado lleva el trazo doble
        {
        const s16 chaflan = (s16) (variante ? 4 : 3);   // radio del redondeo

        for (piece = 0; piece < 8; piece++)
        {
            const bool arriba  = (piece == 0 || piece == 1 || piece == 2);
            const bool abajo   = (piece == 5 || piece == 6 || piece == 7);
            const bool izq     = (piece == 0 || piece == 3 || piece == 5);
            const bool der     = (piece == 2 || piece == 4 || piece == 7);
            const bool esquina = (piece == 0 || piece == 2 || piece == 5 || piece == 7);

            for (row = 0; row < 8; row++)
                for (col = 0; col < 8; col++)
                {
                    // distancia al borde del recuadro, ya descontado el retranqueo
                    const s16 dr = (s16) ((arriba ? row : (7 - row)) - BAR_INSET);
                    const s16 dc = (s16) ((izq    ? col : (7 - col)) - BAR_INSET);
                    bool on = FALSE;

                    if (arriba && dr >= 0 && dr < grosor) on = TRUE;
                    if (abajo  && dr >= 0 && dr < grosor) on = TRUE;
                    if (izq    && dc >= 0 && dc < grosor) on = TRUE;
                    if (der    && dc >= 0 && dc < grosor) on = TRUE;

                    if (esquina)
                    {

                        if (dr < 0 || dc < 0)
                        {
                            on = FALSE;
                        }
                        else if (dr < chaflan && dc < chaflan)
                        {
                            const s16 ar = (s16) (dr - chaflan);
                            const s16 ac = (s16) (dc - chaflan);
                            const s16 d2 = (s16) ((ar * ar) + (ac * ac));
                            const s16 inner = (s16) (chaflan - grosor);
                            on = (d2 <= (s16) (chaflan * chaflan)) && (d2 >= (s16) (inner * inner));
                        }
                    }

                    px[row][col] = on ? circleInk[ink] : 0;
                }

            barPackTile(px, (u16) (BAR_TILE_BASE + (((ink * 2) + variante) * BAR_FRAME_SET) + piece));
        }
        }
      }

}


// pal: PAL0 normalmente. Un planeta superado se pinta con NAME_PAL_DONE,
// donde el indice 15 es amarillo (Menu_setVisible lo deja ahi) -- o sea
// que los MISMOS tiles de tinta 0, que usan ese indice, salen amarillos
// sin necesidad de duplicar el arte.
static void barPutTile(u16 tileIndex, u16 x, u16 y, u16 pal)
{
    VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(pal, 0, FALSE, FALSE, tileIndex), x, y);
}

void Menu_drawPlanetBar(u8 selectedIndex, u8 unlockedCount, const bool *completed)
{
    u8 i;

    VDP_drawText(galaxyName, BAR_X, BAR_GALAXY_Y);

    for (i = 0; i < MENU_PLANET_COUNT; i++)
    {
        const u16 bx    = (u16) (BAR_X + (i * BAR_STRIDE));
        const bool bloqueado = (i >= unlockedCount);
        const bool elegido   = (i == selectedIndex);
        // Superado: el recuadro y el planeta salen amarillos igual que el
        // nombre (user request: "cuando un planeta esta completado, su
        // nombre sale en amarillo, quiero que el cuadrado y el planeta
        // tambien"). No hay tiles nuevos: se pintan los de tinta 0, que
        // usan el indice 15, con la paleta cuyo 15 es amarillo.
        const bool superado  = !bloqueado && (completed != NULL) && completed[i];
        const u16 pal   = (u16) (superado ? NAME_PAL_DONE : PAL0);
        const u8  ink   = (u8) (bloqueado ? 1 : 0);                       // circulo y marco normal
        // El violeta del elegido cederia al amarillo del superado, asi que
        // un planeta superado conserva el amarillo y la seleccion se lee
        // por el GROSOR del marco, que es un canal aparte del color.
        const u8  fink  = (u8) (elegido ? (bloqueado ? 1 : (superado ? 0 : 2)) : ink);
        const u16 frame = (u16) (BAR_TILE_BASE
                                 + ((((u16) fink * 2) + (elegido ? 1u : 0u)) * BAR_FRAME_SET));
        // El hueco de la celda lleva AL PLANETA, dibujado a 16x16 por
        // Menu_setGalaxyLook: el mismo que esta dando vueltas ahi abajo.
        const u16 celda = (u16) (CELL_TILE_BASE + (i * 4));
        const u16 cpal  = (u16) (bloqueado ? PAL0 : (superado ? PAL2 : PAL3));

        barPutTile((u16) (frame + 0), bx, BAR_Y, pal);
        barPutTile((u16) (frame + 1), (u16) (bx + 1), BAR_Y, pal);
        barPutTile((u16) (frame + 1), (u16) (bx + 2), BAR_Y, pal);
        barPutTile((u16) (frame + 2), (u16) (bx + 3), BAR_Y, pal);

        barPutTile((u16) (frame + 3), bx, (u16) (BAR_Y + 1), pal);
        barPutTile((u16) (celda + 0), (u16) (bx + 1), (u16) (BAR_Y + 1), cpal);
        barPutTile((u16) (celda + 1), (u16) (bx + 2), (u16) (BAR_Y + 1), cpal);
        barPutTile((u16) (frame + 4), (u16) (bx + 3), (u16) (BAR_Y + 1), pal);

        barPutTile((u16) (frame + 3), bx, (u16) (BAR_Y + 2), pal);
        barPutTile((u16) (celda + 2), (u16) (bx + 1), (u16) (BAR_Y + 2), cpal);
        barPutTile((u16) (celda + 3), (u16) (bx + 2), (u16) (BAR_Y + 2), cpal);
        barPutTile((u16) (frame + 4), (u16) (bx + 3), (u16) (BAR_Y + 2), pal);

        barPutTile((u16) (frame + 5), bx, (u16) (BAR_Y + 3), pal);
        barPutTile((u16) (frame + 6), (u16) (bx + 1), (u16) (BAR_Y + 3), pal);
        barPutTile((u16) (frame + 6), (u16) (bx + 2), (u16) (BAR_Y + 3), pal);
        barPutTile((u16) (frame + 7), (u16) (bx + 3), (u16) (BAR_Y + 3), pal);

    }

    // Solo el nombre del planeta bajo el cursor, a tamano normal y debajo de
    // SU propio recuadro: el nombre tiene que senalar a cual pertenece. Los 8
    // a la vez ocupaban 32 de las 40 columnas y no dejaban sitio para
    // separarlos sin comerse los margenes.
    {
        const bool bloqueado = (selectedIndex >= unlockedCount);
        const bool superado  = !bloqueado && (completed != NULL) && completed[selectedIndex];

        if (bloqueado)      VDP_setTextPalette(NAME_PAL_LOCKED);
        else if (superado)  VDP_setTextPalette(NAME_PAL_DONE);
        else                VDP_setTextPalette(NAME_PAL_OPEN);

        VDP_drawText(planetNames[selectedIndex],
                     (u16) (BAR_X + (selectedIndex * BAR_STRIDE)), BAR_NAME_Y);
    }

    VDP_setTextPalette(PAL0);   // the rest of the menu goes back to white
}

// Las tres copias de las cuatro tintas: PAL3 para un planeta normal, PAL0 en
// gris para uno cerrado y PAL2 en amarillo para uno terminado. El menu ya
// hacia ese truco con una tinta sola (misma silueta, otra paleta); con cuatro
// hay que preparar las tres rampas.
#define PLANET_INK_FIRST PLANET_PAL_FIRST
static u16 savedPlanetInk[3][PLANET_PAL_COUNT];
static bool planetInkSaved;
static u8 curGalaxyLook;

// Un planeta cerrado sale gris y uno terminado amarillo, igual que antes --
// pero ahora son TRES rampas de cuatro niveles, asi que el gris y el amarillo
// se repiten en las tres: lo que cambia entre rampas es el tono, y esos dos
// estados no tienen tono propio.
static u16 rampaGris(u8 nivel)
{
    static const u8 n[PLANET_RAMP_LEVELS] = { 1, 2, 4, 5 };

    return RGB3_3_3_TO_VDPCOLOR(n[nivel], n[nivel], n[nivel]);
}

static u16 rampaAmarilla(u8 nivel)
{
    static const u8 r[PLANET_RAMP_LEVELS] = { 2, 4, 6, 7 };
    static const u8 g[PLANET_RAMP_LEVELS] = { 2, 4, 6, 7 };
    static const u8 b[PLANET_RAMP_LEVELS] = { 0, 0, 1, 3 };

    return RGB3_3_3_TO_VDPCOLOR(r[nivel], g[nivel], b[nivel]);
}

// El color de la ranura `slot` (0..11) en cada uno de los tres estados:
// 0 = PAL3 normal, 1 = PAL0 cerrado, 2 = PAL2 terminado.
static u16 planetInk(u8 galaxia, u8 cual, u8 slot)
{
    const u8 rampa = (u8) (slot / PLANET_RAMP_LEVELS);
    const u8 nivel = (u8) (slot % PLANET_RAMP_LEVELS);

    if (cual == 1)
        return rampaGris(nivel);
    if (cual == 2)
        return rampaAmarilla(nivel);

    return PlanetArt_tint(galaxia, rampa, nivel);
}

static const u8 planetInkPal[3] = { PAL3, PAL0, PAL2 };

// Dibuja los ocho planetas de esa galaxia y sube sus teselas. Es lo que hace
// que ARRIBA/ABAJO en el menu cambie los graficos y no solo el nombre.
void Menu_setGalaxyLook(u8 galaxia)
{
    // Estaticos y no en la pila: 576 + 288 bytes no caben a gusto en los 2560
    // de pila que tiene esto (maze.c ya se encontro con lo mismo).
    static u8 px[24 * 24];
    static u32 tiles[9 * 8];
    u8 i, j;

    curGalaxyLook = (u8) (galaxia & 7);

    for (i = 0; i < MENU_PLANET_COUNT; i++)
    {
        const u8 lado = planetSide[i];
        const u8 n = (u8) (lado / 8);
        u8 tx, ty, fila, col;
        u16 t = 0;

        PlanetArt_draw(curGalaxyLook, i, lado, px);

        // Las teselas de un sprite van en COLUMNAS: el VDP recorre cada tira
        // vertical entera antes de pasar a la siguiente.
        for (tx = 0; tx < n; tx++)
            for (ty = 0; ty < n; ty++, t++)
                for (fila = 0; fila < 8; fila++)
                {
                    u32 w = 0;

                    for (col = 0; col < 8; col++)
                        w = (w << 4) | px[((((u16) ty * 8) + fila) * lado) + (tx * 8) + col];
                    tiles[(t * 8) + fila] = w;
                }

        VDP_loadTileData(tiles, (u16) (PLANET_TILE_BASE + planetTileAt[i]), (u16) (n * n), DMA);

        // Y el mismo planeta a 16x16 para su celda del selector. Aqui las
        // teselas son de FONDO, asi que van por FILAS: (0,0) (1,0) (0,1) (1,1).
        PlanetArt_draw(curGalaxyLook, i, CELL_SIDE, px);
        for (ty = 0; ty < 2; ty++)
            for (tx = 0; tx < 2; tx++)
                for (fila = 0; fila < 8; fila++)
                {
                    u32 w = 0;

                    for (col = 0; col < 8; col++)
                        w = (w << 4) | px[((((u16) ty * 8) + fila) * CELL_SIDE) + (tx * 8) + col];
                    tiles[((((u16) ty * 2) + tx) * 8) + fila] = w;
                }
        VDP_loadTileData(tiles, (u16) (CELL_TILE_BASE + (i * 4)), 4, DMA);
    }

    // Y las tres rampas de tinta, si el menu esta a la vista. Si no lo esta,
    // Menu_setVisible las pondra al entrar: pisarlas ahora seria pintarle al
    // juego los colores del menu encima.
    if (planetInkSaved)
        for (j = 0; j < 3; j++)
            for (i = 0; i < PLANET_PAL_COUNT; i++)
                PAL_setColor((planetInkPal[j] * 16) + PLANET_INK_FIRST + i,
                             planetInk(curGalaxyLook, j, i));
}

void Menu_loadGraphics(void)
{
    loadTitleTiles();
    loadBarTiles();

    // PAL3 is otherwise unused by the rest of the game (PAL0=maze/text,
    // PAL1=playerShip/mapShip, PAL2=enemyShip) -- dedicated here so the
    // menu's planet/cursor sprites don't disturb any in-game palette.
    PAL_setPalette(PAL3, planetSmall.palette->data, DMA);

    // Matches planetHalfSize[] above: small for the 4 tiniest presets
    // (including the new 1x1 test planet), medium for the next 2, large
    // for the original 2 biggest.
    // Los PNG siguen dando el TAMANO del sprite (8x8, 16x16, 24x24) y nada
    // mas: su dibujo no se sube nunca. Sin SPR_FLAG_AUTO_VRAM_ALLOC ni
    // AUTO_TILE_UPLOAD, el sprite mira a las teselas que Menu_setGalaxyLook
    // deja en PLANET_TILE_BASE, que son las de la galaxia en curso.
    {
        const SpriteDefinition *def[MENU_PLANET_COUNT] = {
            &planetSmall, &planetSmall, &planetSmall, &planetSmall,
            &planetMedium, &planetMedium, &planetLarge, &planetLarge
        };
        u8 i;

        for (i = 0; i < MENU_PLANET_COUNT; i++)
            planetSprites[i] = SPR_addSpriteEx(def[i], 0, 0,
                TILE_ATTR_FULL(PAL3, TRUE, FALSE, FALSE,
                               (u16) (PLANET_TILE_BASE + planetTileAt[i])),
                SPR_FLAG_AUTO_VISIBILITY);
    }
    Menu_setGalaxyLook(0);
    cursorSprite = SPR_addSprite(&cursorArrow, 0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));

    // Sun uses PAL1 (see SUN_INK_INDEX above) -- no PAL_setPalette call
    // here, its color comes from whatever Menu_setVisible pokes into
    // that slot, not from menuSun's own compiled palette data. Position
    // is fixed (the sun never moves), so it's set once here rather than
    // every frame in Menu_update.
    sunSprite = SPR_addSprite(&menuSun, SUN_CENTER_X - SUN_HALF_PX, SUN_CENTER_Y - SUN_HALF_PX,
                               TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    SPR_setDepth(sunSprite, DEPTH_SOL);
    SPR_setDepth(cursorSprite, DEPTH_CURSOR);
    {
        u8 i;

        for (i = 0; i < MENU_PLANET_COUNT; i++)
        {
            planetDepth[i] = DEPTH_DELANTE;
            SPR_setDepth(planetSprites[i], DEPTH_DELANTE);
        }
    }

    Menu_setVisible(FALSE);
}

// Just the sprites, none of the palette hand-over below. Pulled out
// because the ORDER of those two matters across a fade: this screen's
// sprites draw their ink from palette slots that Menu_setVisible hands
// over to gameplay (PAL1's index1 to the ship's violet, PAL3's to the
// locked-door yellow). Hand them over while the planets are still in the
// sprite table and they light up in those colours over a screen that is
// otherwise flat -- which is exactly the "planetas superpuestos" the
// player saw when a run started. main.c hides them, pushes the sprite
// table, and only then lets the palettes change.
void Menu_hideSprites(void)
{
    u8 i;

    for (i = 0; i < MENU_PLANET_COUNT; i++)
        SPR_setVisibility(planetSprites[i], HIDDEN);
    SPR_setVisibility(cursorSprite, HIDDEN);
    SPR_setVisibility(sunSprite, HIDDEN);
}

void Menu_update(u8 selectedIndex, u8 unlockedCount, const bool *completed); // defined below

// Mixes `from` toward `to`, num/den of the way, straight in VDP colour
// space: three 3-bit channels packed as 0000 BBB0 GGG0 RRR0.
static u16 mixColor(u16 from, u16 to, u16 num, u16 den)
{
    const s16 fr = (s16) ((from >> 1) & 7), fg = (s16) ((from >> 5) & 7), fb = (s16) ((from >> 9) & 7);
    const s16 tr = (s16) ((to >> 1) & 7),   tg = (s16) ((to >> 5) & 7),   tb = (s16) ((to >> 9) & 7);
    const u16 r = (u16) (fr + (((tr - fr) * (s16) num) / (s16) den));
    const u16 g = (u16) (fg + (((tg - fg) * (s16) num) / (s16) den));
    const u16 b = (u16) (fb + (((tb - fb) * (s16) num) / (s16) den));

    return (u16) ((b << 9) | (g << 5) | (r << 1));
}

// Brings the menu's own sprites up out of the background, after the
// screen itself has already faded in (user request: "los planetas
// aparecen de fundido tambien una vez que haya terminado el fundido al
// menu"). They are made visible at step 0, when all four of their ink
// slots still hold exactly the background colour and there is nothing to
// see, and then those four are ramped to their real values. The sun, the
// cursor and all three planet looks come up together.
#define MENU_SPRITE_FADE_FRAMES 10  // el doble de rapido (peticion del usuario), era 20

void Menu_fadeInSprites(u8 selectedIndex, u8 unlockedCount, const bool *completed)
{
    const u16 bg = RGB24_TO_VDPCOLOR(MAZE_BG_COLOR);
    const u16 sun = RGB24_TO_VDPCOLOR(0xFFFFFF);
    const u16 done = RGB24_TO_VDPCOLOR(0xFFFF00);
    const u16 normal = planetSmall.palette->data[1];
    const u16 locked = RGB24_TO_VDPCOLOR(LOCKED_PLANET_COLOR);
    u16 f, i;

    for (f = 0; f <= MENU_SPRITE_FADE_FRAMES; f++)
    {
        PAL_setColor(SUN_INK_INDEX, mixColor(bg, sun, f, MENU_SPRITE_FADE_FRAMES));
        PAL_setColor(COMPLETED_INK_INDEX, mixColor(bg, done, f, MENU_SPRITE_FADE_FRAMES));
        PAL_setColor(LOCKED_DOOR_INK_INDEX, mixColor(bg, normal, f, MENU_SPRITE_FADE_FRAMES));
        PAL_setColor(LOCKED_PLANET_INK_INDEX, mixColor(bg, locked, f, MENU_SPRITE_FADE_FRAMES));

        // Los planetas suben con todo lo demas: si sus doce tintas se quedan
        // puestas, aparecen de golpe sobre una pantalla que aun esta negra.
        {
            u8 j, k;

            for (j = 0; j < 3; j++)
                for (k = 0; k < PLANET_PAL_COUNT; k++)
                    PAL_setColor((planetInkPal[j] * 16) + PLANET_INK_FIRST + k,
                                 mixColor(bg, planetInk(curGalaxyLook, j, k),
                                          f, MENU_SPRITE_FADE_FRAMES));
        }

        if (f == 0)
        {
            for (i = 0; i < MENU_PLANET_COUNT; i++)
                SPR_setVisibility(planetSprites[i], VISIBLE);
            SPR_setVisibility(cursorSprite, VISIBLE);
            SPR_setVisibility(sunSprite, VISIBLE);
        }

        // Orbiting all the while (user request: "puedes hacer que los
        // planetas hagan fade in pero ya moviendose?") -- the same
        // per-frame update the menu runs, so they arrive already in
        // motion instead of lighting up where they happen to be parked.
        Menu_update(selectedIndex, unlockedCount, completed);

        SPR_update();
        SYS_doVBlankProcess();
    }
}

// Los ocho se van por arriba o por abajo (dir -1 o +1) y, al volver, entran
// por el lado contrario. Las dos mitades llevan su propio bucle de frames,
// igual que Menu_fadeInSprites: entre una y otra, main.c cambia de galaxia y
// redibuja, que es lo que hace que los que entran ya sean los nuevos.
void Menu_galaxySweepOut(s16 dir, u8 selectedIndex, u8 unlockedCount, const bool *completed)
{
    u16 f;

    for (f = 1; f <= SWEEP_FRAMES; f++)
    {
        sweepOffsetY = (s16) (((s32) dir * SWEEP_DIST * f) / SWEEP_FRAMES);
        Menu_update(selectedIndex, unlockedCount, completed);
        SPR_update();
        SYS_doVBlankProcess();
    }
}

void Menu_galaxySweepIn(s16 dir, u8 selectedIndex, u8 unlockedCount, const bool *completed)
{
    u16 f;

    for (f = SWEEP_FRAMES; f > 0; f--)
    {
        sweepOffsetY = (s16) (((s32) -dir * SWEEP_DIST * f) / SWEEP_FRAMES);
        Menu_update(selectedIndex, unlockedCount, completed);
        SPR_update();
        SYS_doVBlankProcess();
    }

    sweepOffsetY = 0;
}

void Menu_setVisible(bool visible)
{
    u8 i;

    for (i = 0; i < MENU_PLANET_COUNT; i++)
        SPR_setVisibility(planetSprites[i], visible ? VISIBLE : HIDDEN);
    SPR_setVisibility(cursorSprite, visible ? VISIBLE : HIDDEN);
    SPR_setVisibility(sunSprite, visible ? VISIBLE : HIDDEN);
    if (!visible)
        sweepOffsetY = 0;     // nadie se queda a medio barrido

    // Las doce ranuras de tinta de los planetas (cuatro por cada una de las
    // tres paletas que usan: normal, cerrado y terminado). Mismo trato que el
    // resto de prestamos de aqui abajo: se guardan al entrar y se devuelven al
    // salir, para que el juego no herede los colores del menu.
    if (visible)
    {
        u8 j, k;

        if (!planetInkSaved)
        {
            for (j = 0; j < 3; j++)
                for (k = 0; k < PLANET_PAL_COUNT; k++)
                    savedPlanetInk[j][k] =
                        PAL_getColor((planetInkPal[j] * 16) + PLANET_INK_FIRST + k);
            planetInkSaved = TRUE;
        }
        for (j = 0; j < 3; j++)
            for (k = 0; k < PLANET_PAL_COUNT; k++)
                PAL_setColor((planetInkPal[j] * 16) + PLANET_INK_FIRST + k,
                             planetInk(curGalaxyLook, j, k));
    }
    else if (planetInkSaved)
    {
        u8 j, k;

        for (j = 0; j < 3; j++)
            for (k = 0; k < PLANET_PAL_COUNT; k++)
                PAL_setColor((planetInkPal[j] * 16) + PLANET_INK_FIRST + k,
                             savedPlanetInk[j][k]);
        planetInkSaved = FALSE;
    }

    // La fila de recuadros pinta sus circulos y el contorno del
    // seleccionado con el mismo violeta que los planetas de las orbitas.
    if (visible)
    {
        if (!barInkSaved)   // no re-guardar los colores sobre si mismos
        {
            savedBarInk      = PAL_getColor(BAR_INK_INDEX);
            savedNameInk[0]  = PAL_getColor((NAME_PAL_LOCKED * 16) + 15);
            savedNameInk[1]  = PAL_getColor((NAME_PAL_OPEN   * 16) + 15);
            savedNameInk[2]  = PAL_getColor((NAME_PAL_DONE   * 16) + 15);
            barInkSaved = TRUE;
        }
        PAL_setColor(BAR_INK_INDEX, planetSmall.palette->data[1]);
        PAL_setColor((NAME_PAL_LOCKED * 16) + 15, RGB24_TO_VDPCOLOR(LOCKED_PLANET_COLOR));
        PAL_setColor((NAME_PAL_OPEN   * 16) + 15, planetSmall.palette->data[1]);
        PAL_setColor((NAME_PAL_DONE   * 16) + 15, RGB24_TO_VDPCOLOR(0xFFFF00));
    }
    else if (barInkSaved)
    {
        PAL_setColor(BAR_INK_INDEX, savedBarInk);
        PAL_setColor((NAME_PAL_LOCKED * 16) + 15, savedNameInk[0]);
        PAL_setColor((NAME_PAL_OPEN   * 16) + 15, savedNameInk[1]);
        PAL_setColor((NAME_PAL_DONE   * 16) + 15, savedNameInk[2]);
        barInkSaved = FALSE;
    }

    // Flip PAL1's index1 between white (menu showing, spec §32septies)
    // and playerShip's own violet (leaving the menu) -- safe because
    // playerShip/mapShip, the only other things using PAL1, are always
    // hidden for the entire time the menu is visible (main.c).
    if (visible)
        PAL_setColor(SUN_INK_INDEX, RGB24_TO_VDPCOLOR(0xFFFFFF));
    else
        PAL_setColor(SUN_INK_INDEX, PLAYER_SHIP_COLOR);

    // Same idea on PAL2's index1 (spec §45): yellow while the menu
    // shows (for completed-planet sprites, see Menu_update), restored
    // to enemyShip's own violet on the way out -- safe for the same
    // reason, enemySprites are always hidden while the menu is visible.
    if (visible)
        PAL_setColor(COMPLETED_INK_INDEX, RGB24_TO_VDPCOLOR(0xFFFF00));
    else
        PAL_setColor(COMPLETED_INK_INDEX, enemyShip.palette->data[1]);

    // See LOCKED_DOOR_BG_INDEX/LOCKED_DOOR_INK_INDEX's own doc comment --
    // opposite direction from the two overrides above: restore
    // planetSmall's own baked colors here (menu becoming visible, so PAL3
    // needs to look like planets again), and hand the slots over to
    // maze.c's yellow locked-door look the instant the menu hides
    // (gameplay starting, planet sprites all hidden by the loop above).
    if (visible)
    {
        PAL_setColor(LOCKED_DOOR_BG_INDEX, planetSmall.palette->data[0]);
        PAL_setColor(LOCKED_DOOR_INK_INDEX, planetSmall.palette->data[1]);
    }
    else
    {
        PAL_setColor(LOCKED_DOOR_BG_INDEX, RGB24_TO_VDPCOLOR(MAZE_BG_COLOR)); // the game's one background colour (maze.h)
        PAL_setColor(LOCKED_DOOR_INK_INDEX, RGB24_TO_VDPCOLOR(0xFFFF00));
    }

    // And PAL0's index1, which is grey for a locked planet while the menu
    // is up and maze.c's wall colour the rest of the time (see
    // LOCKED_PLANET_INK_INDEX).
    if (visible)
        PAL_setColor(LOCKED_PLANET_INK_INDEX, RGB24_TO_VDPCOLOR(LOCKED_PLANET_COLOR));
    else
        PAL_setColor(LOCKED_PLANET_INK_INDEX, RGB24_TO_VDPCOLOR(MAZE_WALL_COLOR));

    // Fresh entry into the menu always restarts the orbit animation from
    // the same spread-out starting angles -- simpler and just as good
    // visually as trying to resume mid-orbit, and avoids needing to
    // persist orbitAngle[] across a game played in between.
    if (visible)
    {
        u8 j;

        for (j = 0; j < MENU_PLANET_COUNT; j++)
            orbitAngle[j] = FIX16((360 / MENU_PLANET_COUNT) * j);
    }
}

void Menu_update(u8 selectedIndex, u8 unlockedCount, const bool *completed)
{
    u8 i;
    s16 selCx = SUN_CENTER_X, selCy = SUN_CENTER_Y; // fallback, always overwritten below

    for (i = 0; i < MENU_PLANET_COUNT; i++)
    {
        fix16 fx, fy;
        s16 cx, cy;

        // LA ORBITA TAMBIEN ES DE LA GALAXIA (peticion del usuario: orbitas,
        // vectores de movimiento y trayectorias). Cada una achata sus elipses
        // de otra manera, va a otro ritmo y la mitad giran al reves, asi que
        // se nota que has cambiado de sitio antes incluso de mirar el dibujo.
        // El radio en X no se toca: es el que tiene la pantalla de ancho.
        const s16 estira = PlanetArt_orbitStretch(curGalaxyLook);
        const s16 ry = (s16) ((orbitRadiusY[i] * estira) / 100);
        const fix16 paso = (fix16) (((s32) orbitSpeed[i] *
                                     PlanetArt_orbitSpeed(curGalaxyLook)) / 100);

        orbitAngle[i] += (fix16) (paso * PlanetArt_orbitDir(curGalaxyLook));
        while (orbitAngle[i] >= FIX16(360))
            orbitAngle[i] -= FIX16(360);
        while (orbitAngle[i] < 0)
            orbitAngle[i] += FIX16(360);

        F16_computePositionEx(&fx, &fy, FIX16(SUN_CENTER_X), FIX16(SUN_CENTER_Y),
                               orbitAngle[i], FIX16(1), FIX16(orbitRadiusX[i]), FIX16(ry));

        cx = F16_toRoundedInt(fx);
        cy = (s16) (F16_toRoundedInt(fy) + sweepOffsetY);

        SPR_setPosition(planetSprites[i], cx - planetHalfSize[i], cy - planetHalfSize[i]);

        {
            const s8 capa = (s8) ((cy < (s16) (SUN_CENTER_Y + sweepOffsetY))
                                  ? DEPTH_DETRAS : DEPTH_DELANTE);

            if (planetDepth[i] != capa)
            {
                planetDepth[i] = capa;
                SPR_setDepth(planetSprites[i], capa);
            }
        }
        // Three looks, one set of pixels -- which palette the sprite reads
        // is the only difference (spec §45): PAL0 grey for a planet still
        // locked (user request), PAL2 yellow for one already completed,
        // PAL3's own violet for the rest.
        if (i >= unlockedCount)
            SPR_setPalette(planetSprites[i], PAL0);
        else
            SPR_setPalette(planetSprites[i], completed[i] ? PAL2 : PAL3);

        if (i == selectedIndex)
        {
            selCx = cx;
            selCy = cy;
        }
    }

    // Cursor: horizontally centered on the selected planet, its bottom
    // edge CURSOR_GAP_PX above that planet's own top edge (spec §31,
    // "justo encima del planeta") -- tracks the live position computed
    // above, not a separately-maintained angle.
    cursorBobTick++;
    SPR_setPosition(cursorSprite,
                     selCx - CURSOR_HALF_W,
                     selCy - planetHalfSize[selectedIndex] - CURSOR_GAP_PX - (2 * CURSOR_HALF_H)
                       + cursorBob[(cursorBobTick / 3) & 15]);
}
