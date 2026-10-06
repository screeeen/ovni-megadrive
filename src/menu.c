#include "menu.h"
#include "resources.h"
#include "player.h"
#include "maze.h"     // MAZE_TILE_COUNT / MAZE_SCRATCH_TILE_COUNT, for the title tiles
#include "guidemap.h" // GUIDEMAP_TILE_COUNT, same

// 320x224 screen. Sun sits left of dead-center vertically so the
// biggest orbit's top edge (with room for the cursor arrow above
// whichever planet sits there) stays clear of the title text, and its
// bottom edge stays clear of the size-name/hint text below (main.c's
// drawMenu()) -- main.c's drawMenu() moved its title up and its bottom
// text down (spec §32quat) to free up the extra vertical room these
// wider orbits need.
#define SUN_CENTER_X 160
#define SUN_CENTER_Y 142  // bajado: galaxia + fila de recuadros ocupan arriba

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
static Sprite *cursorSprite;

// Ping-pong suavizado del cursor: un paso cada 3 tramas, ciclo de 48.
static const s8 cursorBob[16] = { 0, 1, 1, 2, 2, 2, 1, 1, 0, -1, -1, -2, -2, -2, -1, -1 };
static u16 cursorBobTick;
static Sprite *sunSprite;

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
#define BAR_GALAXY_Y (BAR_Y - 1)

#define BAR_TILE_BASE   (TITLE_TILE_BASE + TITLE_TILES)
#define BAR_FRAME_SET   8                      // piezas por marco
#define BAR_CIRCLE_BASE (BAR_TILE_BASE + 48)   // 48 de marco: 3 tintas x 2 grosores x 8
#define BAR_TILES       (48 + (3 * 3 * 4))

// Editables. Cuatro letras por planeta, mismo orden que sizePresets[].
static const char *const planetNames[MENU_PLANET_COUNT] = {
    "XENU", "ZARG", "QORG", "NYXX", "VOID", "KLOP", "WURM", "ZYGN"
};
static const char *const galaxyName = "GALAXIA XR-13";
static const u8 planetCircleSize[MENU_PLANET_COUNT] = { 0, 0, 0, 0, 1, 1, 2, 2 };
static const u8 circleRadius[3] = { 3, 5, 7 };

// Indice 2 = el violeta de los planetas, copiado desde PAL3 por
// Menu_setVisible; indice 1 = el gris de los bloqueados.
#define BAR_INK_INDEX ((PAL0 * 16) + 2)   // violeta de los planetas
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
    u8 piece, row, col, grosor, variante, size, ink, quad;

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

    for (size = 0; size < 3; size++)
        for (ink = 0; ink < 3; ink++)
            for (quad = 0; quad < 4; quad++)
            {
                const s16 ox = (s16) ((quad & 1) * 8);
                const s16 oy = (s16) ((quad >> 1) * 8);
                const s16 r  = circleRadius[size];

                for (row = 0; row < 8; row++)
                    for (col = 0; col < 8; col++)
                    {
                        // centro del bloque de 16x16 entre los pixeles 7 y 8
                        const s16 dx = (s16) ((ox + col) * 2 - 15);
                        const s16 dy = (s16) ((oy + row) * 2 - 15);
                        const bool dentro = (s16) (dx*dx + dy*dy) <= (s16) (4*r*r);
                        px[row][col] = dentro ? circleInk[ink] : 0;
                    }

                barPackTile(px, (u16) (BAR_CIRCLE_BASE + (((size * 3) + ink) * 4) + quad));
            }
}

static void barPutTile(u16 tileIndex, u16 x, u16 y)
{
    VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, tileIndex), x, y);
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
        const u8  ink   = (u8) (bloqueado ? 1 : 0);                       // circulo y marco normal
        const u8  fink  = (u8) (elegido ? (bloqueado ? 1 : 2) : ink);     // violeta solo al elegido
        const u16 frame = (u16) (BAR_TILE_BASE
                                 + ((((u16) fink * 2) + (elegido ? 1u : 0u)) * BAR_FRAME_SET));
        const u16 circ  = (u16) (BAR_CIRCLE_BASE + (((planetCircleSize[i] * 3) + ink) * 4));

        barPutTile((u16) (frame + 0), bx,             BAR_Y);
        barPutTile((u16) (frame + 1), (u16) (bx + 1), BAR_Y);
        barPutTile((u16) (frame + 1), (u16) (bx + 2), BAR_Y);
        barPutTile((u16) (frame + 2), (u16) (bx + 3), BAR_Y);

        barPutTile((u16) (frame + 3), bx,             (u16) (BAR_Y + 1));
        barPutTile((u16) (circ  + 0), (u16) (bx + 1), (u16) (BAR_Y + 1));
        barPutTile((u16) (circ  + 1), (u16) (bx + 2), (u16) (BAR_Y + 1));
        barPutTile((u16) (frame + 4), (u16) (bx + 3), (u16) (BAR_Y + 1));

        barPutTile((u16) (frame + 3), bx,             (u16) (BAR_Y + 2));
        barPutTile((u16) (circ  + 2), (u16) (bx + 1), (u16) (BAR_Y + 2));
        barPutTile((u16) (circ  + 3), (u16) (bx + 2), (u16) (BAR_Y + 2));
        barPutTile((u16) (frame + 4), (u16) (bx + 3), (u16) (BAR_Y + 2));

        barPutTile((u16) (frame + 5), bx,             (u16) (BAR_Y + 3));
        barPutTile((u16) (frame + 6), (u16) (bx + 1), (u16) (BAR_Y + 3));
        barPutTile((u16) (frame + 6), (u16) (bx + 2), (u16) (BAR_Y + 3));
        barPutTile((u16) (frame + 7), (u16) (bx + 3), (u16) (BAR_Y + 3));

        if (bloqueado)                                VDP_setTextPalette(NAME_PAL_LOCKED);
        else if (elegido)                             VDP_setTextPalette(NAME_PAL_OPEN);
        else if (completed != NULL && completed[i])   VDP_setTextPalette(NAME_PAL_DONE);
        else                                          VDP_setTextPalette(PAL0);

        VDP_drawText(planetNames[i], bx, BAR_NAME_Y);
    }

    VDP_setTextPalette(PAL0);   // el resto del menu vuelve a blanco
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
    planetSprites[0] = SPR_addSprite(&planetSmall,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[1] = SPR_addSprite(&planetSmall,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[2] = SPR_addSprite(&planetSmall,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[3] = SPR_addSprite(&planetSmall,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[4] = SPR_addSprite(&planetMedium, 0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[5] = SPR_addSprite(&planetMedium, 0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[6] = SPR_addSprite(&planetLarge,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[7] = SPR_addSprite(&planetLarge,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    cursorSprite = SPR_addSprite(&cursorArrow, 0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));

    // Sun uses PAL1 (see SUN_INK_INDEX above) -- no PAL_setPalette call
    // here, its color comes from whatever Menu_setVisible pokes into
    // that slot, not from menuSun's own compiled palette data. Position
    // is fixed (the sun never moves), so it's set once here rather than
    // every frame in Menu_update.
    sunSprite = SPR_addSprite(&menuSun, SUN_CENTER_X - SUN_HALF_PX, SUN_CENTER_Y - SUN_HALF_PX,
                               TILE_ATTR(PAL1, TRUE, FALSE, FALSE));

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
#define MENU_SPRITE_FADE_FRAMES 20

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

void Menu_setVisible(bool visible)
{
    u8 i;

    for (i = 0; i < MENU_PLANET_COUNT; i++)
        SPR_setVisibility(planetSprites[i], visible ? VISIBLE : HIDDEN);
    SPR_setVisibility(cursorSprite, visible ? VISIBLE : HIDDEN);
    SPR_setVisibility(sunSprite, visible ? VISIBLE : HIDDEN);

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

        orbitAngle[i] += orbitSpeed[i];
        if (orbitAngle[i] >= FIX16(360))
            orbitAngle[i] -= FIX16(360);

        F16_computePositionEx(&fx, &fy, FIX16(SUN_CENTER_X), FIX16(SUN_CENTER_Y),
                               orbitAngle[i], FIX16(1), FIX16(orbitRadiusX[i]), FIX16(orbitRadiusY[i]));

        cx = F16_toRoundedInt(fx);
        cy = F16_toRoundedInt(fy);

        SPR_setPosition(planetSprites[i], cx - planetHalfSize[i], cy - planetHalfSize[i]);
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
