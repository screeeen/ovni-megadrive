#ifndef _MAZE_H_
#define _MAZE_H_

#include <genesis.h>

// One maze cell = 8x8 px = 1 VDP background tile (user request: double the
// room's cell-grid resolution by drawing the tile art smaller instead of
// scrolling -- was 16x16px/2x2 tiles when MAZE_W/H were 20x14; every
// cell-unit constant elsewhere in the codebase -- door widths, anchor
// depths, prune chance, etc. -- is untouched, so the whole maze just reads
// twice as fine-grained at the same physical size).
// 40x28 cells * 8px = 320x224px, exactly one Mega Drive screen (no scrolling needed).
#define MAZE_TILE_PX    8
#define MAZE_W          40
#define MAZE_H          28

// The room's own interior hub / carve seed -- where Maze_generateRoom's
// carve() always starts (guaranteed-open regardless of door layout), and
// where items.c's fixed item position still anchors. Not the ship's own
// spawn point (that's the insertion room's menu door -- see main.c's
// newGame()). Door POSITIONS along their own borders don't derive from
// this (they're independently random per door, see MapCell's
// doorOffsetN/E/S/W in guidemap.h) -- this is only the room's center,
// not a door coordinate.
#define MAZE_DOOR_COL (MAZE_W / 2)
#define MAZE_DOOR_ROW ((MAZE_H / 2) & ~1)

// mazeTiles occupies this many contiguous VRAM tiles starting at
// TILE_USER_INDEX (see maze.c's BASE_TILE comment): 37 logical 8x8
// cells, one VDP tile each -- floor, 9 wall dither variants x 4 section
// hues (cells 1-36) -- no separate locked-door cell (there's no lock
// concept at all any more, docs/spec-mapa-libre.md §3): every door is
// always open, so it always renders as an ordinary wall cell from the
// room's own hue when sealed shut, and PATH otherwise. Anything else
// built on TILE_USER_INDEX (e.g. guidemap.c's overlay tiles) must start
// after this to avoid overlapping maze.c's tileset in VRAM.
#define MAZE_TILE_COUNT 37

// Number of section hues (spec §18) -- a room has at most 4 doors, so at
// most 4 branches ever grow directly out of the start room, which caps
// how many distinct wall colors are ever needed at once.
#define MAZE_SECTION_COUNT 4

// One representative wall-dither tile per section hue (variant 5 of each
// hue's 9-cell block: absolute cell = hue*9 + 5, see BASE_TILE in maze.c:
// tile = TILE_USER_INDEX + c for absolute cell c). hue must be <
// MAZE_SECTION_COUNT. Other modules can reuse this for a textured
// "duotono" look consistent with the maze's own walls instead of
// introducing flat new art -- guidemap.c's overlay always uses hue 0 (the
// original violet), regardless of section, since the per-section
// coloring is a gameplay-view-only feature (spec §18).
#define MAZE_WALL_DITHER_TILE(hue) (TILE_USER_INDEX + ((hue) * 9) + 5)

// Uploads the maze tileset to VRAM and sets its palette. Call once at boot.
void Maze_loadGraphics(void);

// Carves a new maze (recursive-backtracker, ported from ovni's generateMap.js)
// and leaves it ready to draw. Single-room prototype only (main.c's current
// game loop) -- untouched by the multi-room guide map system below.
void Maze_generate(void);

// Carves a room for the free-roam map (docs/spec-mapa-libre.md §2):
// generated ONCE per game, up front, not per real-time visit -- so
// unlike the old per-visit design this replaces, a room's layout can
// never change between two visits (nothing about it depends on
// progress any more; there is no progress-dependent state left to
// depend on -- no locks, no critical path, docs/spec-mapa-libre.md §3).
// Seeded from the room's center instead of a fixed corner, with a
// 2-cell-wide door forced open on every side passed as TRUE -- every
// active door is ALWAYS open, there's no locked-door concept at all any
// more.
// doorOffsets[4] gives, for each direction (indexed by guidemap.h's
// DOOR_N/E/S/W numeric convention 0/1/2/3, same as
// Maze_generateInsertionRoom's doorDir below -- maze.c doesn't #include
// guidemap.h, see that function's comment), the maze column (N/S
// entries) or maze row (E/W entries) that door sits at. Entries for a
// direction whose doorX is FALSE are ignored. Caller must ensure both
// rooms sharing a door pass the identical offset for it (guidemap.c's
// GuideMap_doorOffset already guarantees this).
// sectionHue (0..MAZE_SECTION_COUNT-1) picks which of the 4 wall-dither
// hue blocks every wall cell in this room is drawn from -- same dither
// shapes/density either way, just a different accent color, so rooms in
// different branches of the map read as different "zones" while
// playing.
// hasItem: TRUE if this room holds a letter (guidemap.h's itemCol/
// itemRow) -- items only ever live in dead-end rooms (exactly 1 active
// door, guidemap.c's selectItemRooms), and the pickup itself sits at
// this room's hub (MAZE_DOOR_COL/MAZE_DOOR_ROW, items.c) -- a THIRD
// point past the door itself, so it's included in this room's own
// verification (see below) whenever this is TRUE.
//
// The actual guarantee (docs/spec-mapa-libre.md §5, replacing the old
// entry<->critical design this function used to implement): ALL of this
// room's active doors (plus its item hub, if it has one) are verified
// MUTUALLY tomb-reachable by an actual BFS simulation, run at generation
// time -- if a layout fails that check, a different one is tried (up to
// MAZE_MAX_SEED_ATTEMPTS times) until one verifies clean. This is only
// affordable because generation now happens once per room (while
// building the map, not during real-time play).
//
// Each retry varies BOTH the carve() wander (roomSeed+something) AND
// buildAllDoorsChain's group1/group2 split (a "rotation" of which door
// the N/E/S/W scan starts from) -- fuzz-confirmed the wander alone
// doesn't help the dominant failure class: it's caused by FIXED anchor/
// border positions (ANCHOR_DEPTH_N/S/E/W, always 2 or W/H-4) coincidentally
// matching another door's random OFFSET, which doesn't change between
// retries at all (a shared door's offset is fixed once picked, agreed
// with the neighboring room across that edge) -- so retrying carve()
// alone reproduced the identical failure 8/8 times in a real case before
// rotation was added. With both varied, measured failure rate (960,000
// simulated door-pairs, host-side fuzzing) dropped from ~22% to ~4.2% --
// better, but NOT close to negligible: rotation can't help the cases
// where the coincidence is exact regardless of grouping (e.g. a N/S
// door's offset landing exactly on ANCHOR_DEPTH_W/E's value creates a
// conflict with ANY door on that perpendicular side, in every possible
// group split). Narrowing guidemap.c's door-offset range away from
// those exact extremes would reduce this further but wasn't done this
// pass -- see docs/spec-mapa-libre.md §9 for the honest status.
//
// Why this couldn't just be proven mathematically instead: a room's
// interior connectivity graph, using pure axis-aligned slide-until-wall
// movement, can have at most 2 "leaf" points that stay mutually
// reachable through a single degree<=2 routing pass, regardless of room
// size or junction width -- a topological constraint (a tree where
// every non-leaf node has degree <=2 can have at most 2 leaves). Up to
// 4 doors (now always simultaneously open, docs/spec-mapa-libre.md §3)
// needs a genuinely different technique to reach all of them at once --
// buildAllDoorsChain's 2-groups-plus-bridge split is that technique, but
// it isn't provably perfect the way the old 2-point-only chain was, so
// generation-time verification+retry is what actually closes the gap to
// a real guarantee.
//
// Split in two (docs/spec-mapa-libre.md §5) because the search itself
// only ever needs to run ONCE, up front, while building the whole map
// (guidemap.c's GuideMap_verifyAllRooms) -- paying the retry cost again
// on every real-time visit would reintroduce exactly the kind of
// per-frame risk this whole redesign exists to avoid:
//   - Maze_findRoomAttempt (below) runs the search, returns which
//     attempt index (0..MAZE_MAX_SEED_ATTEMPTS-1 -- see maze.c, kept
//     internal since nothing outside needs the exact bound, only that
//     it fits guidemap.h's 3-bit seedAttempt field) verified clean.
//   - Maze_generateRoom (immediately below that) takes that SAME index
//     back as `attempt` and just reproduces it -- no search, no
//     verification, deterministic and cheap, safe to call every time
//     the room is actually loaded during play.
u8 Maze_findRoomAttempt(bool doorN, bool doorE, bool doorS, bool doorW,
                         const u8 doorOffsets[4], u16 roomSeed, bool hasItem);
void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        const u8 doorOffsets[4], u8 sectionHue, u16 roomSeed, u8 attempt);

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
// plain bool doorN/E/S/W parameters already do. Also verified+retried at
// generation time now (docs/spec-mapa-libre.md §5, same split as
// Maze_generateRoom/Maze_findRoomAttempt above) -- always exactly 2
// doors, so this is the simple direct door<->door chain (never
// buildAllDoorsChain's group+bridge split), just no longer trusted blind
// the way it used to be. main.c finds the winning attempt ONCE (right
// alongside GuideMap_verifyAllRooms) and passes it back in here every
// time the insertion room is actually (re)generated.
u8 Maze_findInsertionRoomAttempt(u8 doorDir, u8 doorOffset, u8 menuDoorDir, u8 menuDoorOffset, u16 roomSeed);
void Maze_generateInsertionRoom(u8 doorDir, u8 doorOffset, u8 menuDoorDir, u8 menuDoorOffset, u16 roomSeed, u8 attempt);

// Draws the current maze to plane BG_A.
void Maze_draw(void);

// tx/ty in maze-cell units. Out-of-range coordinates count as wall.
bool Maze_isWall(s16 tx, s16 ty);

s16 Maze_startPixelX(void);
s16 Maze_startPixelY(void);

#endif
