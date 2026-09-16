#include <genesis.h>
#include "resources.h"
#include "maze.h"
#include "player.h"
#include "guidemap.h"
#include "enemy.h"
#include "items.h"

typedef enum { STATE_MENU, STATE_PLAYING } GameState;

typedef struct { u8 cols, rows; } SizePreset;

static const SizePreset sizePresets[] = {
    { 6, 4 },
    { 8, 6 },
    { 10, 8 },
};
#define SIZE_PRESET_COUNT 3
#define SIZE_PRESET_DEFAULT 1 // 8x6, the size the spec settled on (§0)

#define ENEMY_COUNT 2

// playerShip's palette (spec §13bis) is 2 colors: index0 (never rendered
// -- Genesis sprite hardware always treats palette index0 as transparent)
// and index1, the ship's actual visible color. mapShip (spec §24, an 8x8
// single-tile silhouette, same size as a guide-map letter) shares PAL1
// too -- same 2 colors, so no separate palette load needed for it.
// Absolute CRAM index of PAL1's index1 (spec §23): used to flash
// whichever of the two sprites is showing white on the guide map,
// without touching shape/tiles or any other palette.
#define PLAYER_SHIP_INK_INDEX ((PAL1 * 16) + 1)

// Blink period while the guide map is open (spec §23, slowed down for
// spec §24): toggled every this many frames, so a full on/off cycle is
// 2x this at 60fps.
#define MAP_BLINK_FRAMES 30

static Player player;
static Sprite *playerSprite;
static Sprite *mapShipSprite;
static Enemy enemies[ENEMY_COUNT];
static Sprite *enemySprites[ENEMY_COUNT];
static u16 mapSeed;
static u8 currentCol, currentRow;
// TRUE while the player is still in the special insertion room (spec
// §27), before currentCol/currentRow are ever set -- gates every place
// that would otherwise read a stale/meaningless (currentCol,currentRow).
static bool inInsertRoom;
// Which border the insertion room's own single door sits on this game
// (spec §29quat: derived once per newGame() as the OPPOSITE side of
// insertLinkDir -- N<->S, E<->W -- so leaving the insertion room and
// arriving at the periphery room reads as one continuous, spatially
// consistent line, same as any normal room-to-room transition; was
// independently randomized before that, spec §29ter). A DOOR_N/E/S/W
// value (guidemap.h).
static u8 insertRoomDoorDir;
static bool mapViewOpen;
static u16 mapBlinkTimer;
static GameState gameState;
static u8 sizePresetIndex = SIZE_PRESET_DEFAULT;

// Deterministic per-room seed (spec §5): same (col,row) under the same
// mapSeed always yields the same roomSeed, so Maze_generateRoom's layout
// persists across re-entries without ever storing it.
static u16 roomSeedFor(u8 col, u8 row)
{
    u32 h = mapSeed;

    h = h * 374761393u + col;
    h = h * 668265263u + row;
    h ^= h >> 15;

    return (u16) h;
}

// EXIT_NORTH/EAST/SOUTH/WEST (player.h) and DOOR_N/E/S/W (guidemap.h) are
// different enumerations (1/2/3/4 vs 0/1/2/3) for the same 4 directions --
// this converts one to the other, needed wherever insertLinkDir (a
// DOOR_x) has to be compared against Player_updateRoom's return value (an
// EXIT_x), spec §29.
static u8 exitDirForDoorDir(u8 doorDir)
{
    switch (doorDir)
    {
        case DOOR_N: return EXIT_NORTH;
        case DOOR_E: return EXIT_EAST;
        case DOOR_S: return EXIT_SOUTH;
        default:     return EXIT_WEST; // DOOR_W
    }
}

// Places the player just inside the border used to enter the room they
// were just loaded into, aligned with that door's actual span (spec §29,
// offset-aware per spec §30) -- used for the insertion-link transition,
// which (unlike enterRoomFrom) isn't stepping to a grid-adjacent
// (col,row), so there's no exitDir to derive this from the usual way.
// "Entering via DOOR_N" is the same physical scenario as enterRoomFrom's
// EXIT_SOUTH case (left the previous room south, entered this one's
// north side), and so on around -- this mirrors that same placement
// table, just indexed by the new room's own entry side instead of the
// old room's exit side. offset is the door's column (N/S) or row (E/W),
// spec §30 -- no longer always MAZE_DOOR_COL/MAZE_DOOR_ROW.
static void positionPlayerEnteringViaDoorDir(u8 doorDir, u8 offset)
{
    switch (doorDir)
    {
        case DOOR_N:
            player.y = MAZE_TILE_PX;
            player.x = offset * MAZE_TILE_PX;
            break;
        case DOOR_S:
            player.y = MAZE_TILE_PX * (MAZE_H - 2);
            player.x = offset * MAZE_TILE_PX;
            break;
        case DOOR_E:
            player.x = MAZE_TILE_PX * (MAZE_W - 2);
            player.y = offset * MAZE_TILE_PX;
            break;
        default: // DOOR_W
            player.x = MAZE_TILE_PX;
            player.y = offset * MAZE_TILE_PX;
            break;
    }
}

// Offset (spec §30) of (col,row)'s door in direction dir -- transparently
// covers the insertion link's extra door too (spec §29): when (col,row)
// is (insertLinkCol,insertLinkRow) and dir is insertLinkDir, that edge
// isn't a real tree door so GuideMap_doorOffset wouldn't have a
// meaningful value for it; insertLinkOffset is used instead. Every other
// case reads the real per-edge value guidemap.c already guarantees
// matches on both sides of that door.
static u8 doorOffsetFor(u8 col, u8 row, u8 dir)
{
    if ((col == insertLinkCol) && (row == insertLinkRow) && (dir == insertLinkDir))
        return insertLinkOffset;

    return GuideMap_doorOffset(col, row, dir);
}

static void loadRoom(u8 col, u8 row)
{
    const MapCell cell = guideMap[row][col];
    const u16 seed = roomSeedFor(col, row);
    // Locked branches (spec §16): a door that exists in the room graph but
    // leads only to letters not due yet gets sealed -- short-circuit skips
    // GuideMap_isRoomLocked when there's no door at all in that direction,
    // so out-of-range neighbor coords are never read. Gated on cell.doorX
    // (the RAW tree bit), never the insertLinkDir-merged doorX below --
    // the insertion link is never part of the lock graph.
    const bool lockedN = cell.doorN && GuideMap_isRoomLocked(col, row - 1);
    const bool lockedE = cell.doorE && GuideMap_isRoomLocked(col + 1, row);
    const bool lockedS = cell.doorS && GuideMap_isRoomLocked(col, row + 1);
    const bool lockedW = cell.doorW && GuideMap_isRoomLocked(col - 1, row);
    const u8 sectionHue = GuideMap_roomSection(col, row); // spec §18: per-branch wall color
    // The insertion room's link (spec §27/§29) punches an extra, always-
    // unlocked door on whichever border side has no grid neighbor at all
    // -- guideMap's own doorN/E/S/W bits are left untouched (BFS/locks/
    // sections/corridor-drawing in guidemap.c never see this), it only
    // affects what gets physically carved into THIS room's own maze.
    const bool isInsertLinkRoom = (col == insertLinkCol) && (row == insertLinkRow);
    const bool doorN = cell.doorN || (isInsertLinkRoom && (insertLinkDir == DOOR_N));
    const bool doorE = cell.doorE || (isInsertLinkRoom && (insertLinkDir == DOOR_E));
    const bool doorS = cell.doorS || (isInsertLinkRoom && (insertLinkDir == DOOR_S));
    const bool doorW = cell.doorW || (isInsertLinkRoom && (insertLinkDir == DOOR_W));
    // Where each of those doors sits along its border (spec §30) --
    // array indices match guidemap.h's DOOR_N/E/S/W numbering (0/1/2/3),
    // same convention maze.c's doorOffsets[4] parameter expects.
    const u8 doorOffsets[4] = {
        doorOffsetFor(col, row, DOOR_N),
        doorOffsetFor(col, row, DOOR_E),
        doorOffsetFor(col, row, DOOR_S),
        doorOffsetFor(col, row, DOOR_W),
    };
    u8 i;

    Maze_generateRoom(doorN, doorE, doorS, doorW,
                       lockedN, lockedE, lockedS, lockedW, doorOffsets, sectionHue, seed);
    Maze_draw();
    Items_drawInRoom(col, row);

    guideMap[row][col].visited = TRUE;

    // Same seed the room's maze uses, so the patrol routes are
    // deterministic per room too (spec-equivalent persistence, see
    // enemy.h). Each enemy draws further from the same reseeded stream,
    // so the two land on different tiles in practice without needing to
    // coordinate explicitly.
    for (i = 0; i < ENEMY_COUNT; i++)
    {
        Enemy_spawnForRoom(&enemies[i], seed);
        SPR_setPosition(enemySprites[i], enemies[i].x, enemies[i].y);
    }
}

// Room transition (spec §7): move to the neighboring cell, regenerate its
// (deterministic) layout, and place the player just inside the opposite
// border, aligned with that door's actual span (spec §30), still heading
// the same direction.
static void enterRoomFrom(u8 exitDir)
{
    u8 enterDoorDir;

    switch (exitDir)
    {
        case EXIT_NORTH: currentRow--; enterDoorDir = DOOR_S; break;
        case EXIT_EAST:  currentCol++; enterDoorDir = DOOR_W; break;
        case EXIT_SOUTH: currentRow++; enterDoorDir = DOOR_N; break;
        default:         currentCol--; enterDoorDir = DOOR_E; break; // EXIT_WEST
    }

    loadRoom(currentCol, currentRow);
    positionPlayerEnteringViaDoorDir(enterDoorDir, doorOffsetFor(currentCol, currentRow, enterDoorDir));
}

static void newGame(void)
{
    mapViewOpen = FALSE;
    mapSeed = random();

    mapCols = sizePresets[sizePresetIndex].cols;
    mapRows = sizePresets[sizePresetIndex].rows;

    GuideMap_generate();
    Items_reset();
    GuideMap_recomputeLocks(); // A is unlocked from the start, B..E sealed (spec §16)

    // The player's actual physical starting point is the special
    // insertion room (spec §27), outside the grid entirely -- NOT
    // (startCol,startRow), which stays the room tree's logical root
    // (BFS/lock/section origin) and is otherwise unrelated to where the
    // ship first appears. currentCol/currentRow are only set once the
    // player leaves the insertion room, into (insertLinkCol,insertLinkRow).
    inInsertRoom = TRUE;
    // Which border its own door sits on: the OPPOSITE side of
    // insertLinkDir (spec §29quat, N<->S / E<->W -- same "+2 mod 4" flip
    // guidemap.c's own static opposite() and enemy.c's Enemy_opposite()
    // use for the same 4-direction pairing), so the insertion room's
    // exit and the periphery room's entrance read as one continuous
    // line instead of two independently-facing doors. GuideMap_generate()
    // (just above) already set insertLinkDir for this game. Stays fixed
    // for the rest of this playthrough, reused identically every time
    // the room gets regenerated (initial spawn, and any later trip back
    // into it).
    insertRoomDoorDir = (u8) ((insertLinkDir + 2) & 3);
    // Same offset as insertLinkOffset (spec §30): opposite directions
    // (N<->S, E<->W) share the same axis, so no translation is needed.
    Maze_generateInsertionRoom(insertRoomDoorDir, insertLinkOffset, mapSeed);
    Maze_draw();
    Items_drawHud();
    Player_spawnAtRoomCenter(&player);
    SPR_setPosition(playerSprite, player.x, player.y);
    SPR_setVisibility(playerSprite, VISIBLE);
    {
        u8 i;
        // No enemies in the insertion room (spec §27) -- they stay
        // hidden until the player reaches their first real room.
        for (i = 0; i < ENEMY_COUNT; i++)
            SPR_setVisibility(enemySprites[i], HIDDEN);
    }
}

static void drawMenu(void)
{
    char buf[16];
    int len;

    VDP_clearPlane(BG_A, TRUE);

    VDP_drawText("OVNI", 18, 6);
    VDP_drawText("TAMANO DE MAPA", 13, 11);

    len = sprintf(buf, "< %d x %d >", sizePresets[sizePresetIndex].cols, sizePresets[sizePresetIndex].rows);
    VDP_drawText(buf, (40 - len) / 2, 13);

    VDP_drawText("PULSA START", 14, 18);
}

// Hard reset combo (user request): A+B+C+UP together, from anywhere
// (menu or mid-game), drops back to the size-select menu. Checked ahead
// of the per-state input handling below so it always takes priority over
// whatever any of those 4 buttons would otherwise do that same frame.
#define RESET_COMBO (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_UP)

static void resetToMenu(void)
{
    u8 i;

    gameState = STATE_MENU;
    mapViewOpen = FALSE;
    inInsertRoom = FALSE; // harmless either way -- newGame() sets it back to TRUE when a new run starts

    SPR_setVisibility(playerSprite, HIDDEN);
    SPR_setVisibility(mapShipSprite, HIDDEN);
    for (i = 0; i < ENEMY_COUNT; i++)
        SPR_setVisibility(enemySprites[i], HIDDEN);

    drawMenu();
}

int main(bool hardReset)
{
    u16 prevState = 0;

    VDP_setPlaneSize(64, 32, TRUE);
    SPR_init();

    Maze_loadGraphics();
    GuideMap_loadGraphics();

    PAL_setPalette(PAL1, playerShip.palette->data, DMA);
    playerSprite = SPR_addSprite(&playerShip, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    // mapShip (spec §24): same PAL1, same 2 colors as playerShip, so no
    // separate palette load -- just a smaller (1 tile, 8x8) silhouette
    // shown instead of playerShip while the guide map is open.
    mapShipSprite = SPR_addSprite(&mapShip, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));

    PAL_setPalette(PAL2, enemyShip.palette->data, DMA);
    {
        u8 i;
        for (i = 0; i < ENEMY_COUNT; i++)
            enemySprites[i] = SPR_addSprite(&enemyShip, 0, 0, TILE_ATTR(PAL2, TRUE, FALSE, FALSE));
    }

    JOY_init();

    gameState = STATE_MENU;
    SPR_setVisibility(playerSprite, HIDDEN);
    SPR_setVisibility(mapShipSprite, HIDDEN);
    {
        u8 i;
        for (i = 0; i < ENEMY_COUNT; i++)
            SPR_setVisibility(enemySprites[i], HIDDEN);
    }
    drawMenu();

    while (TRUE)
    {
        const u16 state = JOY_readJoypad(JOY_1);

        if (((state & RESET_COMBO) == RESET_COMBO) && ((prevState & RESET_COMBO) != RESET_COMBO))
        {
            resetToMenu();
        }
        else if (gameState == STATE_MENU)
        {
            if ((state & BUTTON_LEFT) && !(prevState & BUTTON_LEFT))
            {
                sizePresetIndex = (sizePresetIndex + SIZE_PRESET_COUNT - 1) % SIZE_PRESET_COUNT;
                drawMenu();
            }
            if ((state & BUTTON_RIGHT) && !(prevState & BUTTON_RIGHT))
            {
                sizePresetIndex = (sizePresetIndex + 1) % SIZE_PRESET_COUNT;
                drawMenu();
            }
            if ((state & BUTTON_START) && !(prevState & BUTTON_START))
            {
                gameState = STATE_PLAYING;
                newGame();
            }
        }
        else // STATE_PLAYING
        {
            // Edge-triggered: rotate once per press. Inverted as a test
            // (user request): LEFT turns counter-clockwise, RIGHT
            // clockwise -- swapped from the original mapping below.
            if ((state & BUTTON_LEFT) && !(prevState & BUTTON_LEFT))
                Player_rotateCCW(&player);
            if ((state & BUTTON_RIGHT) && !(prevState & BUTTON_RIGHT))
                Player_rotateCW(&player);
            // Map view disabled while still in the insertion room (spec
            // §27) -- currentCol/currentRow aren't set yet, and there's
            // nothing to preview before the player has even entered the
            // grid.
            if (!inInsertRoom && (state & BUTTON_C) && !(prevState & BUTTON_C))
            {
                mapViewOpen = !mapViewOpen;

                if (mapViewOpen)
                {
                    u8 i;
                    u16 mx, my;

                    GuideMap_drawOverlay();

                    // mapShip (spec §24, 8x8, same size as a map letter)
                    // marks the current room instead of playerSprite
                    // (which hides, same as the enemies) -- centered in
                    // the 24x16px room box: (24-8)/2=8 horizontally,
                    // (16-8)/2=4 vertically. White and blinking (spec
                    // §23): flip the shared ink color, start visible,
                    // reset the blink timer -- the per-frame toggle below
                    // picks it up from here.
                    GuideMap_roomBoxPixelPos(currentCol, currentRow, &mx, &my);
                    SPR_setPosition(mapShipSprite, mx + 8, my + 4);
                    PAL_setColor(PLAYER_SHIP_INK_INDEX, RGB24_TO_VDPCOLOR(0xFFFFFF));
                    SPR_setVisibility(playerSprite, HIDDEN);
                    SPR_setVisibility(mapShipSprite, VISIBLE);
                    mapBlinkTimer = 0;

                    for (i = 0; i < ENEMY_COUNT; i++)
                        SPR_setVisibility(enemySprites[i], HIDDEN);
                }
                else
                {
                    u8 i;

                    Maze_draw();
                    // Restores the ship's normal color (spec §23) -- only
                    // the one word that PLAYER_SHIP_INK_INDEX touched,
                    // the transparent index0 was never changed.
                    PAL_setColor(PLAYER_SHIP_INK_INDEX, playerShip.palette->data[1]);
                    SPR_setVisibility(playerSprite, VISIBLE);
                    SPR_setVisibility(mapShipSprite, HIDDEN);
                    for (i = 0; i < ENEMY_COUNT; i++)
                        SPR_setVisibility(enemySprites[i], VISIBLE);
                }
            }

            if (mapViewOpen)
            {
                // Blink mapShip on the map (spec §23/§24): flip
                // visibility every MAP_BLINK_FRAMES frames while the
                // overlay stays open. Position doesn't need re-setting
                // each frame -- it's static while viewing the map.
                mapBlinkTimer++;
                if (mapBlinkTimer >= MAP_BLINK_FRAMES)
                {
                    mapBlinkTimer = 0;
                    SPR_setVisibility(mapShipSprite, SPR_isVisible(mapShipSprite, FALSE) ? HIDDEN : VISIBLE);
                }
            }
            else if (inInsertRoom)
            {
                // The insertion room (spec §27) has exactly one door, on
                // whichever border insertRoomDoorDir picked for this game
                // (spec §29ter) -- no items/enemies/locks apply here, it
                // lives outside the normal grid entirely.
                const bool doorN = (insertRoomDoorDir == DOOR_N);
                const bool doorE = (insertRoomDoorDir == DOOR_E);
                const bool doorS = (insertRoomDoorDir == DOOR_S);
                const bool doorW = (insertRoomDoorDir == DOOR_W);
                // Only insertRoomDoorDir's own entry is meaningful (the
                // other 3 are FALSE above, so Player_updateRoom never
                // looks at their offset) -- insertLinkOffset is correct
                // for all 4 slots regardless, since opposite directions
                // share the axis (spec §29quat/§30).
                const u8 exitDir = Player_updateRoom(&player, doorN, doorE, doorS, doorW,
                                                      insertLinkOffset, insertLinkOffset,
                                                      insertLinkOffset, insertLinkOffset);

                if (exitDir == exitDirForDoorDir(insertRoomDoorDir))
                {
                    u8 i;

                    // Jump straight to the chosen perimeter room and land
                    // right at its real border opening (spec §29,
                    // insertLinkDir -- loadRoom() already punched it
                    // open, same call as for any of that room's own tree
                    // doors), aligned with that door's actual span (spec
                    // §30).
                    inInsertRoom = FALSE;
                    currentCol = insertLinkCol;
                    currentRow = insertLinkRow;
                    loadRoom(currentCol, currentRow);
                    positionPlayerEnteringViaDoorDir(insertLinkDir, insertLinkOffset);

                    for (i = 0; i < ENEMY_COUNT; i++)
                        SPR_setVisibility(enemySprites[i], VISIBLE);
                }

                SPR_setPosition(playerSprite, player.x, player.y);
            }
            else
            {
                const MapCell cell = guideMap[currentRow][currentCol];
                // The insertion link's extra door (spec §29) is merged in
                // here too, only when this IS that specific room -- by
                // construction insertLinkDir is always a side with no
                // real tree door, so it can never collide with one of
                // cell.doorN/E/S/W below.
                const bool isInsertLinkRoom = (currentCol == insertLinkCol) && (currentRow == insertLinkRow);
                const bool doorN = cell.doorN || (isInsertLinkRoom && (insertLinkDir == DOOR_N));
                const bool doorE = cell.doorE || (isInsertLinkRoom && (insertLinkDir == DOOR_E));
                const bool doorS = cell.doorS || (isInsertLinkRoom && (insertLinkDir == DOOR_S));
                const bool doorW = cell.doorW || (isInsertLinkRoom && (insertLinkDir == DOOR_W));
                const u8 exitDir = Player_updateRoom(&player, doorN, doorE, doorS, doorW,
                                                      doorOffsetFor(currentCol, currentRow, DOOR_N),
                                                      doorOffsetFor(currentCol, currentRow, DOOR_E),
                                                      doorOffsetFor(currentCol, currentRow, DOOR_S),
                                                      doorOffsetFor(currentCol, currentRow, DOOR_W));

                if (exitDir != EXIT_NONE)
                {
                    if (isInsertLinkRoom && (exitDir == exitDirForDoorDir(insertLinkDir)))
                    {
                        // Walked back out through the insertion link:
                        // return to a freshly generated insertion room
                        // (same insertRoomDoorDir as its initial spawn,
                        // spec §29ter -- not re-randomized), entering via
                        // its own single door, same placement its own
                        // arrival uses.
                        u8 i;

                        inInsertRoom = TRUE;
                        Maze_generateInsertionRoom(insertRoomDoorDir, insertLinkOffset, mapSeed);
                        Maze_draw();
                        positionPlayerEnteringViaDoorDir(insertRoomDoorDir, insertLinkOffset);

                        for (i = 0; i < ENEMY_COUNT; i++)
                            SPR_setVisibility(enemySprites[i], HIDDEN);
                    }
                    else
                    {
                        enterRoomFrom(exitDir);
                    }
                }

                SPR_setPosition(playerSprite, player.x, player.y);

                // Skipped on the one frame that just sent the player back
                // into the insertion room (inInsertRoom flips TRUE above)
                // -- currentCol/currentRow are now stale (still pointing
                // at the periphery room), and there's nothing to collect
                // or patrol in the insertion room anyway.
                if (!inInsertRoom)
                {
                    // Physical contact pickup, order enforced (spec §13):
                    // does nothing unless (currentCol,currentRow) holds
                    // the next letter due AND the ship's box overlaps it.
                    if (Items_tryCollect(currentCol, currentRow, player.x, player.y))
                    {
                        Maze_draw();           // wipes the now-collected letter's tile
                        Items_drawInRoom(currentCol, currentRow); // no-op here, kept for symmetry with loadRoom
                        Items_drawHud();
                        // Unlocks the next branch (spec §16); the current
                        // room's own doors never change from this (items
                        // only live in dead ends), it only affects rooms
                        // not yet loaded -- they pick up the new lock
                        // state next time loadRoom() regenerates them.
                        GuideMap_recomputeLocks();
                    }

                    // Routine patrol, no player interaction yet.
                    {
                        u8 i, j;

                        for (i = 0; i < ENEMY_COUNT; i++)
                            Enemy_update(&enemies[i]);

                        // Bounce off each other: reverse both on overlap.
                        // A brief 1-frame overlap before they separate is
                        // imperceptible at 60fps and there's no damage/
                        // health model yet to make it matter.
                        for (i = 0; i < ENEMY_COUNT; i++)
                            for (j = i + 1; j < ENEMY_COUNT; j++)
                                if (Enemy_overlaps(&enemies[i], &enemies[j]))
                                {
                                    enemies[i].dir = Enemy_opposite(enemies[i].dir);
                                    enemies[j].dir = Enemy_opposite(enemies[j].dir);
                                }

                        for (i = 0; i < ENEMY_COUNT; i++)
                            SPR_setPosition(enemySprites[i], enemies[i].x, enemies[i].y);
                    }
                }
            }
        }

        // FPS debug readout (user request), top-right corner on BG_B --
        // same plane/high-priority trick Items_drawHud uses, so it stays
        // visible over BG_A's low-priority maze/menu tiles without those
        // needing to coordinate with it. SYS_getFPS() must be called
        // exactly once per frame (its own doc comment) -- this is that
        // one call, unconditional regardless of gameState.
        {
            char buf[8];
            int len = sprintf(buf, "FPS%lu", SYS_getFPS());

            VDP_setTextPriority(1);
            VDP_drawTextBG(BG_B, buf, 40 - len - 1, 0);
            VDP_setTextPriority(0);
        }

        prevState = state;

        SPR_update();

        SYS_doVBlankProcess();
    }

    return 0;
}
