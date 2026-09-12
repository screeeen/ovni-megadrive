#ifndef _GUIDEMAP_H_
#define _GUIDEMAP_H_

#include <genesis.h>

// Largest grid the menu's size presets can select (main.c) -- fixes the
// static array sizes below. The ACTIVE size for the current game is
// (mapCols, mapRows), set by main.c before calling GuideMap_generate();
// every loop/bounds-check in guidemap.c uses those, not these two.
#define MAX_MAP_COLS 10
#define MAX_MAP_ROWS 8

typedef enum { CELL_EMPTY, CELL_ROOM } CellType;

typedef struct
{
    u8 type   : 1;   // CellType
    u8 doorN  : 1;
    u8 doorE  : 1;
    u8 doorS  : 1;
    u8 doorW  : 1;
    u8 visited: 1;
} MapCell;

#define DOOR_N 0
#define DOOR_E 1
#define DOOR_S 2
#define DOOR_W 3

// % chance a leaf room (exactly 1 door) reverts to CELL_EMPTY after the tree
// is carved (spec §4.2). The start room is never a candidate.
#define PRUNE_CHANCE_PERCENT 25

// Minimum goal distance, as a % of the start room's eccentricity in the
// pruned tree (spec §4.3) -- not a fixed hop count, so it scales with
// mapCols/mapRows automatically.
#define GOAL_MIN_DISTANCE_PERCENT 65

// Active grid size for the current game, chosen from the menu's presets
// (main.c) -- set BOTH before calling GuideMap_generate(). Must be <=
// MAX_MAP_COLS/MAX_MAP_ROWS.
extern u8 mapCols, mapRows;

extern MapCell guideMap[MAX_MAP_ROWS][MAX_MAP_COLS];
extern u8 startCol, startRow;
extern u8 goalCol, goalRow;

// Carves a new room tree via randomized Prim's algorithm (spec §4.1), grown
// from a random (startCol, startRow); prunes some leaves (spec §4.2); then
// picks (goalCol, goalRow) at least GOAL_MIN_DISTANCE_PERCENT of the start
// room's eccentricity away, reachable by construction since it's the same
// spanning tree (spec §4.3). guideMap/startCol/startRow/goalCol/goalRow are
// all valid once this returns.
void GuideMap_generate(void);

bool GuideMap_hasDoor(u8 col, u8 row, u8 dir);

// Uploads the overlay tileset to VRAM (right after maze.c's tileset --
// see MAZE_TILE_COUNT) and sets up PAL2 for the current-room highlight.
// Call once at boot, alongside Maze_loadGraphics().
void GuideMap_loadGraphics(void);

// Draws the fog-of-war overlay (spec §6) to BG_A, replacing whatever was
// there (the current room's maze) -- caller is responsible for calling
// Maze_draw() again to restore it when the overlay closes. Only cells with
// visited==TRUE are drawn, each as a small room-shaped box (open side =
// active door, closed side = wall) connected by corridor tiles; the
// (curCol,curRow) room is highlighted in a different palette.
void GuideMap_drawOverlay(u8 curCol, u8 curRow);

#endif
