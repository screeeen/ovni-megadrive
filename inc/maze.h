#ifndef _MAZE_H_
#define _MAZE_H_

#include <genesis.h>

// One maze cell = 16x16 px = 2x2 VDP background tiles.
// 20x14 cells * 16px = 320x224px, exactly one Mega Drive screen (no scrolling needed).
#define MAZE_TILE_PX    16
#define MAZE_W          20
#define MAZE_H          14

// The room's own interior hub -- the letter's cell, the point every tombo
// room's generated graph is built around (maze.c's generateRoomTombo), and
// where items.c's fixed item position anchors (spec §5/§13). The insertion
// room no longer anchors its own path or its player spawn here (main.c's
// newGame() spawns at the mission door itself instead -- see player.h's
// Player_spawnAtRoomCenter for why). Door POSITIONS along their own
// borders don't derive from this either (spec §30: they're independently
// random per door, see MapCell's doorOffsetN/E/S/W in guidemap.h) -- this
// is only ever a room's center, not a door coordinate.
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

// Item-letter coloring (user request: "los colores de las letras tienen
// que tener el mismo color de su puerta") -- an exception to this file's
// own MAZE_WALL_DITHER_TILE comment above ("guidemap.c's overlay always
// uses hue 0... since per-section coloring is a gameplay-view-only
// feature"): a letter now shows that hue too, so it visually matches the
// room/door it lives behind, and yellow while that door is still locked
// (matching maze.c's own locked-door wall tiles, menu.c's
// LOCKED_DOOR_INK_INDEX). Call one of these two right before a
// VDP_drawText/VDP_drawTextBG call that draws an item's letter, then
// Maze_restoreTextColor() immediately after -- every OTHER text draw in
// the game (HUD, FPS, control-mode label...) assumes the default (PAL0,
// hue 0's violet) is restored in between.
void Maze_setTextColorForHue(u8 hue);
void Maze_setTextColorLocked(void);
void Maze_restoreTextColor(void);

// Room generation ("tombo", a Tomb of the Mask homage, user request:
// "borra el codigo para generar las habitaciones en modo normal... nos
// vamos a enfocar en tombo" -- the earlier MAZE_ROOMGEN_CARVE recursive-
// backtracker pipeline, and the menu switch that picked between it and
// this, are gone; tombo is the only generator now). Built for that game's
// movement (main.c's CONTROL_TOMB: the ship slides in a straight line
// until a wall stops it and can only turn from a stop) -- the only
// control scheme it guarantees anything for. It builds an OPEN room --
// walls are only ~10% of the interior, a scatter of small obstacles (each
// one gives the ship stops on every side, so there are many nodes and
// many routes between them, not a single corridor), plus a few
// deliberately placed ones next to each door and near the hub so those
// are always reachable -- then ACCEPTS the room only after simulating
// actual slides over the finished grid (see maze.c's tomboValidate): from
// every way of entering the room (each active door, and the spawn for the
// insertion room) every active door must be leavable from every stop the
// ship can reach, and the hub (the letter) must be crossed -- so the ship
// can never be stuck, whichever door it wants next. Rooms are also
// puzzles (user request: the player has to work out how to reach the
// doors and the letter): the validator counts the slides a solution
// takes and demands a minimum for each door and for the letter, easing
// off in tiers only for the rare door layouts where nothing harder turns
// up (see maze.c's puzzleTiers). Rejected layouts are re-rolled (bounded
// attempts from the room's seed, then bounded deterministic ones that
// were verified exhaustively host-side over every door subset x offset
// combination) -- a trivial open-interior fallback exists only as a
// safety net for if every single one of those somehow fails, and carries
// no puzzle guarantee; it's never actually been observed to trigger.

// Accepted-attempt cache (see maze.c's attempt codes): one byte per room
// remembers which generation attempt was accepted, so re-entering a room
// replays only that attempt -- the very same layout, without repeating
// the search. Slots 0..79 are for grid rooms (main.c passes row *
// MAX_MAP_COLS + col; MAX_MAP_COLS * MAX_MAP_ROWS must not exceed
// MAZE_ROOM_CACHE_SLOTS - 1), the last one is the insertion room's.
#define MAZE_ROOM_CACHE_SLOTS  81
#define MAZE_INSERT_CACHE_SLOT 80

// One whole cache bank (MAZE_ROOM_CACHE_SLOTS bytes) per planet (spec
// §52, user request: "no podria calcularse todo antes de empezar el
// juego? antes de cargar el menu?") -- main.c precomputes EVERY planet's
// rooms (and plant totals) once at boot, before the menu ever shows, so
// a bank has to survive for the rest of the session instead of being
// thrown away the moment a different planet is selected. Must equal
// main.c's SIZE_PRESET_COUNT/menu.h's MENU_PLANET_COUNT -- checked by a
// _Static_assert in main.c (maze.c doesn't include menu.h/know about
// presets, same reasoning ITEM_COUNT/MAX_MAP_COLS already document
// elsewhere in this codebase for similar cross-module constants).
#define MAZE_MAP_SLOTS 8

// Selects which of the MAZE_MAP_SLOTS cache banks Maze_generateRoom's
// cacheSlot parameter and Maze_clearRoomCache() operate on, until the
// next call changes it. Call once before generating/scanning a
// particular planet's rooms (main.c's boot-time precompute loop, and
// newGame() picking the one the player is actually about to play).
void Maze_setActiveMapSlot(u8 slot);

// Forgets every cached attempt in the CURRENTLY ACTIVE bank (see
// Maze_setActiveMapSlot) -- every other bank is untouched. Call only
// when that bank's own layout inputs are about to change; normal play
// never needs this any more (every bank is already correct from the
// boot-time precompute pass, for the entire session -- a planet's
// mapSeed never changes after that).
void Maze_clearRoomCache(void);

// Uploads the maze tileset to VRAM and sets its palette. Call once at boot.
void Maze_loadGraphics(void);

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
void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        bool lockedN, bool lockedE, bool lockedS, bool lockedW,
                        const u8 doorOffsets[4], u8 sectionHue, u16 roomSeed, u8 cacheSlot);

// Carves the special "insertion room" (spec §27): the very first room
// the player ever sees, outside the normal room-tree grid entirely (its
// only relationship to the grid is via guidemap.c's insertLinkCol/
// insertLinkRow/insertLinkDir/insertLinkOffset, which main.c uses both to
// know where to send the player once they leave, and to punch open a
// matching real border opening on that specific side of the target room
// -- spec §29, not a blind teleport into its center). Deterministic from
// roomSeed, a single 1-cell-wide corridor between its two doors (see
// maze.c's carveLPath) rather than tombo's usual generator -- this room
// never held a real puzzle of its own. Always a single fixed
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

// Full-screen static/white-noise effect (user request, refined further:
// "más granular... veo tiles"), drawn to BG_A through PAL1 -- genuine
// per-pixel random noise, regenerated into a small dedicated block of VRAM
// tiles every call (see maze.c's own doc comment), not a shuffle of fixed
// pre-drawn shapes. Caller (main.c) is responsible for pointing PAL1's
// index1 at white before the first call (same CRAM slot/trick as the
// map-ship blink, PLAYER_SHIP_INK_INDEX) and restoring both it and the
// real screen once the effect ends. Call once per frame while it's active.
void Maze_drawNoiseFrame(void);

// Debug overlay (user request): one small dot per node of the room's
// slide graph -- every cell the ship can stop in. Draws nothing for a
// room that exhausted every generation attempt and fell back to a
// trivial open interior. Call right after Maze_draw(), every time
// Maze_draw() is called.
void Maze_drawDebugGraph(void);

// One line of dots/dashes along every navigable edge of the room's slide
// graph -- every cell a slide actually crosses, not just the node
// endpoints Maze_drawDebugGraph draws (user request: "pinte lineas en
// todas las aristas navegables por la nave"). Toggled by its own combo
// in main.c; Maze_draw() calls this (and Maze_drawDebugGraph) itself
// whenever Maze_setDebugEdgesVisible(TRUE) is in effect, every time it
// redraws the room -- no separate call needed from main.c.
void Maze_setDebugEdgesVisible(bool on);
bool Maze_debugEdgesVisible(void);
void Maze_drawDebugEdges(void);

// Debug panel backdrop (user request: the debug overlay's text was
// unreadable where it sits over the maze). Paints a solid tile into BG_A
// under the given text rows so main.c's debug text, drawn separately to
// BG_B, has real contrast instead of the maze bleeding through every
// glyph's own transparent pixels. Call once when the panel turns on, not
// every frame -- the box itself never changes. Maze_draw() undoes it
// (call that when the panel turns off).
void Maze_drawDebugBackdrop(u8 firstRow, u8 rows);

// tx/ty in maze-cell units. Out-of-range coordinates count as wall.
bool Maze_isWall(s16 tx, s16 ty);

// Read-only view of the just-generated room's own slide graph (same data
// Maze_drawDebugGraph() draws dots for) -- spec §48, so a caller (plants.c)
// can place things only on cells the ship can actually reach by sliding.
// 0 nodes means no graph guarantee for this room (the carve fallback, or
// the insertion room, which never builds one).
u16 Maze_slideNodeCount(void);
// Cell position (maze units) of slide-graph node `index` (0..Maze_slideNodeCount()-1).
void Maze_slideNodePos(u16 index, s16 *outX, s16 *outY);
// The node reached by sliding from node `index` in direction `dir` (0=N,
// 1=E, 2=S, 3=W, same convention as doorOffsets[4] above), or a negative
// value if that slide doesn't lead to another node. Every cell strictly
// between the two nodes' positions is itself a real, reachable slide cell.
s16 Maze_slideEdge(u16 index, u8 dir);

#endif
