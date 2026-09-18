#ifndef _MAZE_H_
#define _MAZE_H_

#include <genesis.h>

// One maze cell = 16x16 px = 2x2 VDP background tiles.
// 20x14 cells * 16px = 320x224px, exactly one Mega Drive screen (no scrolling needed).
#define MAZE_TILE_PX    16
#define MAZE_W          20
#define MAZE_H          14

// The room's own interior hub / carve seed -- where Maze_generateRoom's
// carve() always starts (guaranteed-open regardless of door layout), and
// where Player_spawnAtRoomCenter/items.c's fixed item position/
// positionPlayerEnteringViaDoorDir's insertion-room landing spot all
// still anchor (spec §5/§13/§27). Door POSITIONS along their own borders
// no longer derive from this (spec §30: they're independently random per
// door, see MapCell's doorOffsetN/E/S/W in guidemap.h) -- this is now
// only the room's center, not a door coordinate.
#define MAZE_DOOR_COL (MAZE_W / 2)
#define MAZE_DOOR_ROW ((MAZE_H / 2) & ~1)

// mazeTiles occupies this many contiguous VRAM tiles starting at
// TILE_USER_INDEX (see maze.c's BASE_TILE/CELL_ROW_TILES comment: 74
// cols x 2 rows of 8x8 subtiles -- 37 logical 16x16 cells: floor, 9 wall
// dither variants x 4 section hues (spec §18, cells 1-36) -- no separate
// locked-door cell (spec §19: a sealed door now reuses an ordinary wall
// cell from the room's own hue instead of a distinct shape). Anything
// else built on TILE_USER_INDEX (e.g. guidemap.c's overlay tiles) must
// start after this to avoid overlapping maze.c's tileset in VRAM.
#define MAZE_TILE_COUNT 148

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
// but the border span is filled with an ordinary wall cell (spec §19:
// same randomWallVariant() as any other wall, not a distinct shape)
// instead of punched open, so Maze_isWall() blocks it exactly like a
// wall -- must only be TRUE where the matching doorX is also TRUE.
// doorOffsets[4] (spec §30) gives, for each direction (indexed by
// guidemap.h's DOOR_N/E/S/W numeric convention 0/1/2/3, same as
// Maze_generateInsertionRoom's doorDir below -- maze.c doesn't #include
// guidemap.h, see that function's comment), the maze column (N/S
// entries) or maze row (E/W entries) that door sits at -- no longer
// always centered on MAZE_DOOR_COL/MAZE_DOOR_ROW. Entries for a
// direction whose doorX is FALSE are ignored. Caller must ensure both
// rooms sharing a door pass the identical offset for it (guidemap.c's
// GuideMap_doorOffset already guarantees this).
// sectionHue (spec §18, 0..MAZE_SECTION_COUNT-1) picks which of the 4
// wall-dither hue blocks every wall cell in this room is drawn from --
// same dither shapes/density either way, just a different accent color,
// so rooms in different branches of the map read as different "zones"
// while playing.
// entryDir/criticalDir (spec §46, user request: rooms must always let
// the ship reach the door it actually needs using TOMB-mode sliding,
// which can only ever stop where the current direction is blocked by a
// wall -- a plain windy maze routinely traps a needed branch behind a
// junction a slide sails straight past, fuzz-confirmed at a ~82%
// failure rate before this fix) -- entryDir is which door the player is
// entering THIS load through, criticalDir is which door (same numeric
// convention, or GUIDEMAP_NO_CRITICAL_DIR's value 0xFF for "none needed"
// -- maze.c doesn't #include guidemap.h, see this function's other
// comments on that) leads toward whatever the ship actually needs next.
// When criticalDir is a real direction different from entryDir, an
// extra guaranteed-safe path is carved between those two doors' anchor
// points ON TOP of the normal maze, walling off any side branch along
// it so nothing else can turn it into an unreachable-by-sliding
// junction -- every OTHER door in the room keeps the normal, not
// specifically tomb-guaranteed, layout.
void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        bool lockedN, bool lockedE, bool lockedS, bool lockedW,
                        const u8 doorOffsets[4], u8 sectionHue, u16 roomSeed,
                        u8 entryDir, u8 criticalDir);

// Carves the special "insertion room" (spec §27): the very first room
// the player ever sees, outside the normal room-tree grid entirely (its
// only relationship to the grid is via guidemap.c's insertLinkCol/
// insertLinkRow/insertLinkDir/insertLinkOffset, which main.c uses both to
// know where to send the player once they leave, and to punch open a
// matching real border opening on that specific side of the target room
// -- spec §29, not a blind teleport into its center). Deterministic from
// roomSeed, same carve algorithm as Maze_generateRoom, seeded from the
// room's own center exactly like every other room. Always a single fixed
// wall-dither cell throughout instead of the usual random mix -- a
// deliberately uniform look distinct from every other room in the game.
// Two doors now (spec §36, user request: "tiene que haber una salida en
// la sala de extracción para que el usuario vuelva a salir al menu"):
// doorDir/doorOffset is the ORIGINAL mission door -- picks which border
// it sits on (spec §29ter: randomized once per game via the opposite of
// insertLinkDir, spec §29quat) and where along it (spec §30, same value
// the periphery room's own matching opening uses, since N/S and their
// opposite S/N -- same for E/W -- always share that axis); walking
// through it leads to/from insertLinkCol/insertLinkRow. menuDoorDir is
// the NEW menu-exit door, always perpendicular to doorDir (main.c picks
// it as (doorDir+1)&3, so it's never the same or opposite side) -- walking
// through it always returns straight to the menu (main.c), regardless of
// how much progress this run has made; menuDoorOffset is caller-supplied
// too, same as doorOffset, even though (unlike the mission door) it never
// needs to line up with any other room's opening -- main.c just picks a
// fixed centered value once, kept there as the single source of truth
// instead of duplicating a centering formula in both files. Both
// directions accept guidemap.h's DOOR_N/E/S/W values (0/1/2/3) by
// numeric convention rather than #including guidemap.h here, to keep
// maze.c decoupled from the guide-map module the way Maze_generateRoom's
// plain bool doorN/E/S/W parameters already do.
void Maze_generateInsertionRoom(u8 doorDir, u8 doorOffset, u8 menuDoorDir, u8 menuDoorOffset, u16 roomSeed);

// Draws the current maze to plane BG_A.
void Maze_draw(void);

// tx/ty in maze-cell units. Out-of-range coordinates count as wall.
bool Maze_isWall(s16 tx, s16 ty);

s16 Maze_startPixelX(void);
s16 Maze_startPixelY(void);

#endif
