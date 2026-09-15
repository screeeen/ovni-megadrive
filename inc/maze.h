#ifndef _MAZE_H_
#define _MAZE_H_

#include <genesis.h>

// One maze cell = 16x16 px = 2x2 VDP background tiles.
// 20x14 cells * 16px = 320x224px, exactly one Mega Drive screen (no scrolling needed).
#define MAZE_TILE_PX    16
#define MAZE_W          20
#define MAZE_H          14

// Door span in tile units, shared by the multi-room system: N/S doors are
// centered on column MAZE_DOOR_COL (2 cells wide), E/W doors on row
// MAZE_DOOR_ROW. Single source of truth for maze.c's door anchors/punch
// coordinates and player.c's exit detection (spec §5, §7) -- keep them
// aligned, don't recompute this differently in either file.
#define MAZE_DOOR_COL (MAZE_W / 2)
#define MAZE_DOOR_ROW ((MAZE_H / 2) & ~1)

// mazeTiles occupies this many contiguous VRAM tiles starting at
// TILE_USER_INDEX (see maze.c's BASE_TILE/CELL_ROW_TILES comment: 22
// cols x 2 rows of 8x8 subtiles -- 11 logical 16x16 cells: floor, 9 wall
// dither variants, 1 locked-door cell). Anything else built on
// TILE_USER_INDEX (e.g. guidemap.c's overlay tiles) must start after this
// to avoid overlapping maze.c's tileset in VRAM.
#define MAZE_TILE_COUNT 44

// One representative wall-dither subtile (variant 5's top-left quarter,
// see BASE_TILE/CELL_ROW_TILES in maze.c: subtile = TILE_USER_INDEX + 2*c
// for wall variant c). Other modules can reuse this for a textured
// "duotono" look consistent with the maze's own walls instead of
// introducing flat new art.
#define MAZE_WALL_DITHER_TILE (TILE_USER_INDEX + (2 * 5))

// Representative subtile (top-left quarter) of the locked-door cell (index
// 10 in maze_tiles.png, an hourglass/X shape distinct from every dither
// wall variant). guidemap.c reuses it the same way it reuses
// MAZE_WALL_DITHER_TILE, to mark locked rooms on the guide map overlay
// with the same visual language as the in-room blocked doors (spec §16).
#define MAZE_LOCKED_DOOR_TILE (TILE_USER_INDEX + (2 * 10))

// Uploads the maze tileset to VRAM and sets its palette. Call once at boot.
void Maze_loadGraphics(void);

// Carves a new maze (recursive-backtracker, ported from ovni's generateMap.js)
// and leaves it ready to draw. Single-room prototype only (main.c's current
// game loop) -- untouched by the multi-room guide map system below.
void Maze_generate(void);

// Carves a room for the Mapa Guía system (docs/spec-mapa-guia.md §5):
// deterministic from roomSeed (same seed -> byte-identical grid, so a room's
// layout persists across re-entries without storing it), seeded from the
// room's center instead of a fixed corner, with a 2-cell-wide door forced
// open on every side passed as TRUE.
// lockedN/E/S/W (spec §16) mark which of those doors, despite existing in
// the room graph, are sealed for now: the interior carve is unaffected,
// but the border span is filled with the locked-door cell instead of
// punched open, so Maze_isWall() blocks it exactly like a wall -- must
// only be TRUE where the matching doorX is also TRUE.
void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        bool lockedN, bool lockedE, bool lockedS, bool lockedW,
                        u16 roomSeed);

// Draws the current maze to plane BG_A.
void Maze_draw(void);

// tx/ty in maze-cell units. Out-of-range coordinates count as wall.
bool Maze_isWall(s16 tx, s16 ty);

s16 Maze_startPixelX(void);
s16 Maze_startPixelY(void);

#endif
