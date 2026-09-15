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
// TILE_USER_INDEX (see maze.c's BASE_TILE/CELL_ROW_TILES comment: 76
// cols x 2 rows of 8x8 subtiles -- 38 logical 16x16 cells: floor, 9 wall
// dither variants x 4 section hues (spec §18, cells 1-36), 1 locked-door
// cell (cell 37, always last regardless of hue). Anything else built on
// TILE_USER_INDEX (e.g. guidemap.c's overlay tiles) must start after this
// to avoid overlapping maze.c's tileset in VRAM.
#define MAZE_TILE_COUNT 152

// Number of section hues (spec §18) -- a room has at most 4 doors, so at
// most 4 branches ever grow directly out of the start room, which caps
// how many distinct wall colors are ever needed at once.
#define MAZE_SECTION_COUNT 4

// One representative wall-dither subtile per section hue (variant 5's
// top-left quarter of each hue's 9-cell block: absolute cell = hue*9 + 5,
// see BASE_TILE/CELL_ROW_TILES in maze.c: subtile = TILE_USER_INDEX + 2*c
// for absolute cell c). hue must be < MAZE_SECTION_COUNT. Other modules
// can reuse this for a textured "duotono" look consistent with the
// maze's own walls instead of introducing flat new art -- guidemap.c's
// overlay always uses hue 0 (the original violet), regardless of
// section, since the per-section coloring is a gameplay-view-only
// feature (spec §18).
#define MAZE_WALL_DITHER_TILE(hue) (TILE_USER_INDEX + (2 * (((hue) * 9) + 5)))

// Representative subtile (top-left quarter) of the locked-door cell
// (absolute cell 37, always last in maze_tiles.png regardless of section
// hue -- an hourglass/X shape distinct from every dither wall variant).
// guidemap.c reuses it the same way it reuses MAZE_WALL_DITHER_TILE, to
// mark locked rooms on the guide map overlay with the same visual
// language as the in-room blocked doors (spec §16).
#define MAZE_LOCKED_DOOR_TILE (TILE_USER_INDEX + (2 * 37))

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
// sectionHue (spec §18, 0..MAZE_SECTION_COUNT-1) picks which of the 4
// wall-dither hue blocks every wall cell in this room is drawn from --
// same dither shapes/density either way, just a different accent color,
// so rooms in different branches of the map read as different "zones"
// while playing.
void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        bool lockedN, bool lockedE, bool lockedS, bool lockedW,
                        u8 sectionHue, u16 roomSeed);

// Draws the current maze to plane BG_A.
void Maze_draw(void);

// tx/ty in maze-cell units. Out-of-range coordinates count as wall.
bool Maze_isWall(s16 tx, s16 ty);

s16 Maze_startPixelX(void);
s16 Maze_startPixelY(void);

#endif
