#ifndef _MAZE_H_
#define _MAZE_H_

#include <genesis.h>

// One maze cell = 16x16 px = 2x2 VDP background tiles.
// 20x12 cells * 16px = 320x192px, and the 32px band left over at the top
// of the 320x224 screen is the HUD's (see MAZE_ORIGIN_* below) -- still
// exactly one Mega Drive screen, no scrolling needed. An even number of
// rows, by request. Measured over 20000 generated rooms, height costs
// slide-graph nodes smoothly and parity does nothing on its own (14 rows:
// 33.3 nodes/room, 13: 31.2, 12: 30.1, all of them with the same
// generation success rate) -- what an even height really buys is the band
// below, 4 rows instead of 2, which is what makes every HUD line fit.
// Palette entry 0: the backdrop the VDP shows wherever nothing is drawn,
// and the background of every maze tile -- so this one value is the
// colour the menu, every room and the fades all sit on (user request:
// "haz el background negro en todos los casos del juego", then "quiero
// un azul oscuro"). Published so menu.c can hand the same colour to the
// locked-door tiles instead of repeating the literal.
#define MAZE_BG_COLOR 0x000024 // dark blue, nearly black (user request) -- blue level 1 of the 7 the VDP has

// One maze cell = 8x8 px = exactly one VDP background tile (user
// request: rooms that vary with the planet, "cuanto mas grande sea el
// planeta, mas grande es la habitacion"). It used to be 16px/2x2 tiles,
// which capped a room at 20x12 cells -- one screen, and there is nowhere
// to grow without scrolling. Halving the cell doubles the grid on both
// axes for the same 320x192 play area, so the biggest planet gets 40x24
// cells instead. The ship halves with it (8x8, see main.c's playerShip)
// -- that is what makes the room genuinely bigger rather than just
// finer: the room is the same pixels, the ship is half the size.
//
// MAZE_W/MAZE_H are now the MAXIMUM a room can be (40x24 = the full
// screen below the HUD), and only ever dimension the static grids.
// The size a room actually IS comes from Maze_setRoomSize() below and
// is read back with Maze_roomW()/Maze_roomH() -- inside maze.c itself
// MAZE_W/MAZE_H are redefined to those runtime values, see the top of
// that file.
#define MAZE_TILE_PX    8
// log2(MAZE_TILE_PX). Converting a room pixel to a cell is a SHIFT, not a
// divide, because it has to floor toward minus infinity: a coordinate one
// pixel outside the room must land on cell -1 so Maze_isWall() reports it
// out of range, and a signed divide would round it to 0 and read a real
// cell instead. Derived from MAZE_TILE_PX and checked against it by a
// _Static_assert in maze.c -- player.c and enemy.c each had this shift
// hardcoded to 4 for the old 16px cell, which is exactly the kind of
// silent drift that survives a clean compile.
#define MAZE_TILE_SHIFT 3
#define MAZE_MAX_W      40
#define MAZE_MAX_H      24

// The size the CURRENT room actually is. Every call site that used to
// read the old fixed 20x12 constants keeps working unchanged -- it just
// reads the live value now. MAZE_MAX_W/MAZE_MAX_H above are only for
// dimensioning storage. (maze.c redefines these two to its own variables
// so its generator loops don't pay a cross-module call per cell.)
u8 Maze_roomW(void);
u8 Maze_roomH(void);
#define MAZE_W ((s16) Maze_roomW())
#define MAZE_H ((s16) Maze_roomH())

// The HUD's own band at the TOP of the screen (user request: "reserva
// fila de tiles superior para pintar el hud, porque ahora se solapa con
// el juego y no se aprecia"). 4 VDP text rows = 32px = exactly the two
// maze rows the room gave up for it, so nothing else about the screen
// changes. Wide enough for every HUD line there is -- rows 0 and 1 (the
// room id/FPS and the letter/plant trackers) and INSERT_STATUS_ROW's
// own, which used to be drawn over the room.
//
// Room coordinates are untouched -- still 0..MAZE_H-1 measured from the
// room's own top-left -- and every conversion from a maze cell
// to a screen position adds these instead: MAZE_ORIGIN_ROW for tile/text
// rows, MAZE_ORIGIN_PX for sprite pixels. Anything that isn't the room
// (the menu, the guide-map overlay, the intro, the HUD itself) addresses
// the full screen as it always did.
#define MAZE_HUD_ROWS   4
#define MAZE_ORIGIN_ROW MAZE_HUD_ROWS
#define MAZE_ORIGIN_PX  (MAZE_HUD_ROWS * 8)

// The room's own interior hub -- the letter's cell, the point every tombo
// room's generated graph is built around (maze.c's generateRoomTombo), and
// where items.c's fixed item position anchors (spec §5/§13). The insertion
// room no longer anchors its own path or its player spawn here (main.c's
// newGame() spawns at the mission door itself instead -- see player.h's
// Player_spawnAtRoomCenter for why). Door POSITIONS along their own
// borders don't derive from this either (spec §30: they're independently
// random per door, see MapCell's doorOffsetN/E/S/W in guidemap.h) -- this
// is only ever a room's center, not a door coordinate.
// Follows the room's live size now that that varies per planet (see
// MAZE_W/MAZE_H above), so this stays the room's own centre whatever
// shape it has. maze.c has its own cheap copy of the same expression.
u8 Maze_hubX(void);
u8 Maze_hubY(void);
#define MAZE_DOOR_COL ((s16) Maze_hubX())
#define MAZE_DOOR_ROW ((s16) Maze_hubY())

// Room footprint. Every room rolls its OWN shape when it generates --
// a random set of cuts out of its bounding box: corners, bites out of a
// side, solid islands in the middle (user request: "quiero que sean
// mucho mas aleatorias y distintas"; it used to be one fixed shape per
// planet, picked from a 6-entry enum). Cells inside the box but outside
// the shape are permanent wall, which is all the slide validator ever
// needs to know -- it only asks Maze_isWall, so a shaped room needs no
// special casing anywhere.
//
// The shape is rolled from the generation ATTEMPT's seed, so one that
// turns out unplayable is rejected and re-rolled like any bad layout,
// and the accepted attempt reproduces it exactly on re-entry. It never
// cuts the lane behind an active door or the hub, which is what lets
// each room be shaped independently of its neighbours: guidemap decides
// a door's position first and the shape works around it.
//
// variety is how many cuts a room may roll (0 = always a plain box).
void Maze_setRoomSize(u8 w, u8 h, u8 variety);

s16 Maze_originPxX(void);
s16 Maze_originPxY(void);
// Same two, in 8px VDP tile/text units, for VDP_drawText call sites.
u16 Maze_originColumn(void);
u16 Maze_originRow(void);

// The range a door's offset along border `dir` (0=N,1=E,2=S,3=W) may
// take for the current footprint: the span of that side that the shape
// actually reaches, inset 2 cells from each corner the way the old fixed
// DOOR_COL_MIN/MAX in guidemap.c did. guidemap.c asks this instead of
// deriving the range from MAZE_W/MAZE_H, so a cut-out corner never gets
// a door placed into it.
void Maze_doorOffsetRange(u8 dir, u8 *lo, u8 *hi);

// mazeTiles occupies this many contiguous VRAM tiles starting at
// TILE_USER_INDEX: 10 logical cells (floor plus the 9 wall dither
// variants) and, now that a cell is 8x8, exactly one VDP tile each --
// it used to be 40, 4 subtiles per 16x16 cell. No separate locked-door
// cell (spec §19: a sealed door reuses an ordinary wall cell, drawn
// through a different palette). Anything else built on TILE_USER_INDEX
// (e.g. guidemap.c's overlay tiles) must start after this to avoid
// overlapping maze.c's tileset in VRAM.
#define MAZE_TILE_COUNT 10

// What maze.c claims in VRAM BEYOND those: the 24-tile pool
// Maze_drawNoiseFrame rewrites every call, plus the single solid tile the
// debug panel's backdrop uses. Published so anything else stacking tiles
// after maze.c's (guidemap.c, menu.c's big title) can say where the free
// space starts instead of each one guessing.
#define MAZE_SCRATCH_TILE_COUNT (24 + 1)

// Item-letter coloring: a letter is drawn yellow while the door it lives
// behind is still locked (matching maze.c's own locked-door wall tiles,
// menu.c's LOCKED_DOOR_INK_INDEX). Call Maze_setTextColorLocked() right
// before such a VDP_drawText/VDP_drawTextBG call, then
// Maze_restoreTextColor() immediately after -- every OTHER text draw in
// the game (HUD, FPS...) assumes the default is restored in between.
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
void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        bool lockedN, bool lockedE, bool lockedS, bool lockedW,
                        const u8 doorOffsets[4], u16 roomSeed, u8 cacheSlot);

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

// The same solid black tile, as any rectangle of it (x/y/w/h in 8px tile
// units). main.c paints one behind the "no map signal" banner so its
// white letters read against something instead of against moving static
// -- and repaints it every frame, since the static owns BG_A.
void Maze_drawSolidBox(u16 x, u16 y, u16 w, u16 h);

// TRUE if a plant placed on this cell can be collected no matter which
// door the ship came in by -- the intersection of what is crossable from
// every door of the room. The slide graph alone is not enough for that:
// it is seeded from all doors at once, while the player only ever enters
// through one, and a slide is directed. See maze.c's own doc comment.
bool Maze_isPlantSafe(s16 tx, s16 ty);

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
