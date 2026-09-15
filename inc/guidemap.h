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

// Item rooms (spec §13): letters A..A+ITEM_COUNT-1, one per dead-end room
// (exactly 1 door), picked by farthest-point sampling so they end up
// spread apart from the start AND from each other, not clustered.
// itemCol[n]/itemRow[n] is where letter n lives; items.c owns which ones
// are collected.
#define ITEM_COUNT 5
extern u8 itemCol[ITEM_COUNT];
extern u8 itemRow[ITEM_COUNT];

// Carves a new room tree via randomized Prim's algorithm (spec §4.1), grown
// from a random (startCol, startRow); prunes some leaves (spec §4.2); then
// picks (goalCol, goalRow) at least GOAL_MIN_DISTANCE_PERCENT of the start
// room's eccentricity away, reachable by construction since it's the same
// spanning tree (spec §4.3); then picks itemCol[]/itemRow[] (spec §13).
// guideMap/startCol/startRow/goalCol/goalRow/itemCol/itemRow are all valid
// once this returns, and so is the per-room section (spec §18, see
// GuideMap_roomSection below) -- it's purely structural (final door
// topology only), so it's computed here and never needs recomputing
// afterwards, unlike the item-dependent lock state.
void GuideMap_generate(void);

bool GuideMap_hasDoor(u8 col, u8 row, u8 dir);

// Which branch (0..MAZE_SECTION_COUNT-1, maze.h) growing directly out of
// the start room this room belongs to (spec §18) -- every room hanging
// off the same direct child of the start shares one number, the start
// room itself is section 0. main.c passes this into Maze_generateRoom's
// sectionHue so each branch of the map reads as its own colored zone
// while playing. Valid once GuideMap_generate() returns.
u8 GuideMap_roomSection(u8 col, u8 row);

// Recomputes which rooms are locked (spec §16): a room is locked if every
// path from the start to it crosses at least one door whose entire far
// side (the whole branch beyond it, not just the immediate room) contains
// no item at or before the one currently due (Items_isUnlocked). Call once
// after GuideMap_generate()+Items_reset() (newGame) and again every time
// Items_tryCollect() returns TRUE -- locking only ever loosens as
// nextIndex advances, never the reverse, so a room already visited can
// never become locked later. Read back via GuideMap_isRoomLocked().
void GuideMap_recomputeLocks(void);

bool GuideMap_isRoomLocked(u8 col, u8 row);

// Uploads the overlay tileset to VRAM (right after maze.c's tileset --
// see MAZE_TILE_COUNT) and sets up PAL2 for the current-room highlight.
// Call once at boot, alongside Maze_loadGraphics().
void GuideMap_loadGraphics(void);

// Draws the fog-of-war overlay (spec §6, tightened by spec §17) to BG_A,
// replacing whatever was there (the current room's maze) -- caller is
// responsible for calling Maze_draw() again to restore it when the
// overlay closes. A cell is drawn -- box, corridor stubs into it, and any
// item letter it holds -- only if visited==TRUE; everything else (never
// entered, including every locked room, spec §16, since a locked room can
// never be visited) is fully omitted, not shown differently. The
// (curCol,curRow) room is highlighted with a solid fill instead of the
// dither used for other visited rooms.
void GuideMap_drawOverlay(u8 curCol, u8 curRow);

#endif
