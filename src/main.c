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

static Player player;
static Sprite *playerSprite;
static Enemy enemies[ENEMY_COUNT];
static Sprite *enemySprites[ENEMY_COUNT];
static u16 mapSeed;
static u8 currentCol, currentRow;
static bool mapViewOpen;
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

static void loadRoom(u8 col, u8 row)
{
    const MapCell cell = guideMap[row][col];
    const u16 seed = roomSeedFor(col, row);
    // Locked branches (spec §16): a door that exists in the room graph but
    // leads only to letters not due yet gets sealed -- short-circuit skips
    // GuideMap_isRoomLocked when there's no door at all in that direction,
    // so out-of-range neighbor coords are never read.
    const bool lockedN = cell.doorN && GuideMap_isRoomLocked(col, row - 1);
    const bool lockedE = cell.doorE && GuideMap_isRoomLocked(col + 1, row);
    const bool lockedS = cell.doorS && GuideMap_isRoomLocked(col, row + 1);
    const bool lockedW = cell.doorW && GuideMap_isRoomLocked(col - 1, row);
    const u8 sectionHue = GuideMap_roomSection(col, row); // spec §18: per-branch wall color
    u8 i;

    Maze_generateRoom(cell.doorN, cell.doorE, cell.doorS, cell.doorW,
                       lockedN, lockedE, lockedS, lockedW, sectionHue, seed);
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
// border, aligned with the door's span, still heading the same direction.
static void enterRoomFrom(u8 exitDir)
{
    switch (exitDir)
    {
        case EXIT_NORTH: currentRow--; break;
        case EXIT_EAST:  currentCol++; break;
        case EXIT_SOUTH: currentRow++; break;
        case EXIT_WEST:  currentCol--; break;
    }

    loadRoom(currentCol, currentRow);

    switch (exitDir)
    {
        case EXIT_NORTH:
            player.y = MAZE_TILE_PX * (MAZE_H - 2);
            player.x = MAZE_DOOR_COL * MAZE_TILE_PX;
            break;
        case EXIT_SOUTH:
            player.y = MAZE_TILE_PX;
            player.x = MAZE_DOOR_COL * MAZE_TILE_PX;
            break;
        case EXIT_EAST:
            player.x = MAZE_TILE_PX;
            player.y = MAZE_DOOR_ROW * MAZE_TILE_PX;
            break;
        case EXIT_WEST:
            player.x = MAZE_TILE_PX * (MAZE_W - 2);
            player.y = MAZE_DOOR_ROW * MAZE_TILE_PX;
            break;
    }
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

    currentCol = startCol;
    currentRow = startRow;

    loadRoom(currentCol, currentRow);
    Items_drawHud();
    Player_spawnAtRoomCenter(&player);
    SPR_setPosition(playerSprite, player.x, player.y);
    SPR_setVisibility(playerSprite, VISIBLE);
    {
        u8 i;
        for (i = 0; i < ENEMY_COUNT; i++)
            SPR_setVisibility(enemySprites[i], VISIBLE);
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

int main(bool hardReset)
{
    u16 prevState = 0;

    VDP_setPlaneSize(64, 32, TRUE);
    SPR_init();

    Maze_loadGraphics();
    GuideMap_loadGraphics();

    PAL_setPalette(PAL1, playerShip.palette->data, DMA);
    playerSprite = SPR_addSprite(&playerShip, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));

    PAL_setPalette(PAL2, enemyShip.palette->data, DMA);
    {
        u8 i;
        for (i = 0; i < ENEMY_COUNT; i++)
            enemySprites[i] = SPR_addSprite(&enemyShip, 0, 0, TILE_ATTR(PAL2, TRUE, FALSE, FALSE));
    }

    JOY_init();

    gameState = STATE_MENU;
    SPR_setVisibility(playerSprite, HIDDEN);
    {
        u8 i;
        for (i = 0; i < ENEMY_COUNT; i++)
            SPR_setVisibility(enemySprites[i], HIDDEN);
    }
    drawMenu();

    while (TRUE)
    {
        const u16 state = JOY_readJoypad(JOY_1);

        if (gameState == STATE_MENU)
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
            if ((state & BUTTON_C) && !(prevState & BUTTON_C))
            {
                mapViewOpen = !mapViewOpen;

                if (mapViewOpen)
                {
                    u8 i;

                    GuideMap_drawOverlay(currentCol, currentRow);
                    SPR_setVisibility(playerSprite, HIDDEN);
                    for (i = 0; i < ENEMY_COUNT; i++)
                        SPR_setVisibility(enemySprites[i], HIDDEN);
                }
                else
                {
                    u8 i;

                    Maze_draw();
                    SPR_setVisibility(playerSprite, VISIBLE);
                    for (i = 0; i < ENEMY_COUNT; i++)
                        SPR_setVisibility(enemySprites[i], VISIBLE);
                }
            }

            if (!mapViewOpen)
            {
                const MapCell cell = guideMap[currentRow][currentCol];
                const u8 exitDir = Player_updateRoom(&player, cell.doorN, cell.doorE, cell.doorS, cell.doorW);

                if (exitDir != EXIT_NONE)
                    enterRoomFrom(exitDir);

                SPR_setPosition(playerSprite, player.x, player.y);

                // Physical contact pickup, order enforced (spec §13): does
                // nothing unless (currentCol,currentRow) holds the next
                // letter due AND the ship's box overlaps it.
                if (Items_tryCollect(currentCol, currentRow, player.x, player.y))
                {
                    Maze_draw();           // wipes the now-collected letter's tile
                    Items_drawInRoom(currentCol, currentRow); // no-op here, kept for symmetry with loadRoom
                    Items_drawHud();
                    // Unlocks the next branch (spec §16); the current
                    // room's own doors never change from this (items only
                    // live in dead ends), it only affects rooms not yet
                    // loaded -- they pick up the new lock state next time
                    // loadRoom() regenerates them.
                    GuideMap_recomputeLocks();
                }

                // Routine patrol, no player interaction yet.
                {
                    u8 i, j;

                    for (i = 0; i < ENEMY_COUNT; i++)
                        Enemy_update(&enemies[i]);

                    // Bounce off each other: reverse both on overlap. A
                    // brief 1-frame overlap before they separate is
                    // imperceptible at 60fps and there's no damage/health
                    // model yet to make it matter.
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

        prevState = state;

        SPR_update();

        SYS_doVBlankProcess();
    }

    return 0;
}
