#ifndef _GUIDEMAP_H_
#define _GUIDEMAP_H_

#include <genesis.h>

// Largest grid the menu's size presets can select (main.c) -- fixes the
// static array sizes below. The ACTIVE size for the current game is
// (mapCols, mapRows), set by main.c before calling GuideMap_generate();
// every loop/bounds-check in guidemap.c uses those, not these two.
// Reverted to 10x8 (spec §32bis, user request) -- spec §32's 2 larger
// presets (which needed 14x12) are gone again.
#define MAX_MAP_COLS 10
#define MAX_MAP_ROWS 8

// mapTiles occupies this many contiguous VRAM tiles right after
// maze.c's own tileset (guidemap.c's MAP_TILE_BASE = TILE_USER_INDEX +
// MAZE_TILE_COUNT). Exposed publicly so other modules that load their
// own tileset after both of these (e.g. menu.c) can compute where their
// own tiles start, the same way maze.h's MAZE_TILE_COUNT already lets
// guidemap.c do this for itself.
#define GUIDEMAP_TILE_COUNT 10

typedef enum { CELL_EMPTY, CELL_ROOM } CellType;

typedef struct
{
    u8 type   : 1;   // CellType
    u8 doorN  : 1;
    u8 doorE  : 1;
    u8 doorS  : 1;
    u8 doorW  : 1;
    u8 visited: 1;
    // TRUE once this room's enemy has been killed by the player (user
    // request: "los enemigos no reaparecen, si los matas en una room no
    // vuelven a aparecer en esa room") -- checked by main.c's loadRoom()
    // every time this room is (re)entered: an enemy is only ever spawned
    // if this is still FALSE. Persists for the rest of the run (reset only
    // by clearMap(), i.e. a new game or the menu resetting the whole map),
    // same lifetime as `visited` above. Read/written via
    // GuideMap_isEnemyDead/GuideMap_markEnemyDead so main.c doesn't need
    // to know the MapCell layout.
    u8 enemyDead: 1;
    // Where each active door sits along its border (spec §30): a maze
    // column (for doorN/doorS) or maze row (for doorE/doorW), in
    // MAZE_TILE_PX cell units -- no longer always the room's own center.
    // Meaningless when the matching doorX above is FALSE. Both rooms
    // sharing an edge always carry the same value for it (openDoor sets
    // both sides together), so a room's own door lines up exactly with
    // its neighbor's matching door.
    u8 doorOffsetN, doorOffsetE, doorOffsetS, doorOffsetW;
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

// Which perimeter room, and which of its (necessarily neighbor-less)
// border sides, the special insertion room's single door leads into
// (spec §27/§29) -- a REAL border opening on that room's insertLinkDir
// side, not a blind teleport into its center. main.c's actual physical
// starting point for the player, distinct from startCol/startRow (which
// stays the room tree's logical root: BFS/lock/section origin, unrelated
// to where the ship first appears). insertLinkDir is DOOR_N/E/S/W; by
// construction it can never coincide with a real door direction the room
// already has toward a grid neighbor (only sides genuinely missing a
// neighbor are ever offered as candidates), so main.c can always tell
// the two apart unambiguously.
// Where insertLinkDir's border opening sits, spec §30 -- same units/axis
// convention as MapCell's doorOffsetN/E/S/W (a maze column for N/S, a
// maze row for E/W). Shared by both ends of the link: the periphery
// room's own opening on its insertLinkDir side, AND the insertion room's
// single door (main.c passes this same value to Maze_generateInsertionRoom
// too) -- N/S and their opposite S/N share the column axis, E/W and their
// opposite W/E share the row axis, so no translation is needed between
// the two sides, just reuse the one value.
extern u8 insertLinkOffset;
extern u8 insertLinkCol, insertLinkRow, insertLinkDir;

// Item rooms (spec §13): letters A..A+itemCount-1, one per dead-end room
// (exactly 1 door), picked by farthest-point sampling so they end up
// spread apart from the start AND from each other, not clustered.
// itemCol[n]/itemRow[n] is where letter n lives; items.c owns which ones
// are collected. ITEM_COUNT is the largest itemCount any preset can pick
// (main.c) -- fixes the static array sizes below, same MAX_/active
// relationship as MAX_MAP_COLS/mapCols above. itemCount itself is set by
// main.c before calling GuideMap_generate() (spec §33: "fases mas
// pequeñas", 1-4 letter presets alongside the original 5-letter ones).
#define ITEM_COUNT 5
extern u8 itemCount;
extern u8 itemCol[ITEM_COUNT];
extern u8 itemRow[ITEM_COUNT];

// Carves a new room tree via randomized Prim's algorithm (spec §4.1), grown
// from a random (startCol, startRow); prunes some leaves (spec §4.2); picks
// (insertLinkCol, insertLinkRow, insertLinkDir) among the map's perimeter
// rooms and their free border sides (spec §27/§29); then picks (goalCol,
// goalRow) at least GOAL_MIN_DISTANCE_PERCENT of the start room's
// eccentricity away, reachable by construction since it's the same
// spanning tree (spec §4.3); then picks itemCol[]/itemRow[] (spec §13).
// guideMap/startCol/startRow/goalCol/goalRow/insertLinkCol/insertLinkRow/
// insertLinkDir/itemCol/itemRow are all valid once this returns, and so is
// the per-room section (spec §18, see GuideMap_roomSection below) -- it's
// purely structural (final door topology only), so it's computed here and
// never needs recomputing afterwards, unlike the item-dependent lock state.
void GuideMap_generate(void);

bool GuideMap_hasDoor(u8 col, u8 row, u8 dir);

// Same value as guideMap[row][col]'s doorOffsetN/E/S/W field for dir
// (spec §30) -- a thin accessor so callers outside guidemap.c don't need
// to know the MapCell layout to read it. Meaningless if
// GuideMap_hasDoor(col,row,dir) is FALSE.
u8 GuideMap_doorOffset(u8 col, u8 row, u8 dir);

// Recomputes which rooms are locked (spec §16, user request: "quiero que
// las compuertas cerradas esten unicamente en la misma habitacion donde
// esta la letra"): only a not-yet-due letter's own dead-end room
// (Items_isUnlocked) is ever locked -- never an intermediate room on the
// way to it, and never the whole branch beyond some earlier door the way
// this used to work. Since an item room's single door IS the room, this
// also directly answers "is this door sealed" for whichever ordinary room
// borders it. Call once after GuideMap_generate()+Items_reset() (newGame)
// and again every time Items_tryCollect() returns TRUE -- locking only
// ever loosens as nextIndex advances, never the reverse, so a room already
// visited can never become locked later. Read back via
// GuideMap_isRoomLocked().
void GuideMap_recomputeLocks(void);

bool GuideMap_isRoomLocked(u8 col, u8 row);

// See MapCell's own enemyDead field doc comment above: TRUE once (col,row)'s
// enemy has been killed, in which case main.c's loadRoom() must not spawn a
// new one there. GuideMap_markEnemyDead() is main.c's own call, right when
// a melee kill lands.
bool GuideMap_isEnemyDead(u8 col, u8 row);
void GuideMap_markEnemyDead(u8 col, u8 row);

// Snapshots/restores those bits whole, the same way plants.c's own
// Plants_saveMask/restoreMask do for collected plants (user request: "si
// la nave muere... quiero que guardes el estado del planeta, con sus
// estrellas y enemigos ya desaparecidos"). GuideMap_generate() clears
// every one of them along with the rest of the map, so a planet being
// resumed has to put them back afterwards or its enemies all come back to
// life -- which no other part of the save would ever reveal, since an
// enemy leaves nothing else behind when it dies.
void GuideMap_saveEnemyDead(bool out[MAX_MAP_ROWS][MAX_MAP_COLS]);
void GuideMap_restoreEnemyDead(const bool in[MAX_MAP_ROWS][MAX_MAP_COLS]);

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
// never be visited) is fully omitted, not shown differently. Every
// visited room, current or not, is drawn fully solid (spec §23) -- no
// longer takes a (curCol,curRow) parameter, since marking the current
// room is now main.c's job (the blinking white ship sprite, positioned
// via GuideMap_roomBoxPixelPos), not this function's. The 5 item rooms
// are the one exception (spec §21): each always gets its letter and a
// hollow-border box drawn, regardless of visited/locked state -- unless
// it was already drawn by the main pass (visited), in which case that
// normal look (plus its letter, same as before) is left alone instead
// of doubled up.
void GuideMap_drawOverlay(void);

// Debug (user request: "si el user pulsa START quiero que el mapa enseñe
// todas las habitaciones disponibles en el mapa sin ocultarlas") -- draws
// every room of the map, with its corridors and its letter, instead of
// only the ones already walked into. Spec §17's fog of war is exactly
// what this lifts, and nothing else: it changes no state, so turning it
// back off hides the unvisited rooms again as if it had never been on.
// main.c toggles it with B while the overlay is up (A held) and redraws.
void GuideMap_setRevealAll(bool on);
bool GuideMap_revealAll(void);

// Pixel position (BG_A tile units x8) of (col,row)'s room box top-left on
// the guide-map overlay (spec §22) -- main.c uses this to reposition the
// ship sprite over the current room's box instead of hiding it while the
// overlay is open, so the sprite itself marks the player's position on
// the map (no new art -- the existing 16x16 playerShip sprite is reused
// as-is; it fits within the box's 24x16px footprint with a small
// centering offset the caller applies).
void GuideMap_roomBoxPixelPos(u8 col, u8 row, u16 *outX, u16 *outY);

// The same, for the insertion/extraction room's own box -- which has no
// (col,row) of its own, since it sits outside the grid. main.c needs it to
// put the blinking ship marker on the right box when the map is opened
// from in there (user request: "desde la sala de insercion el mapa... tiene
// que estar disponible").
void GuideMap_insertRoomBoxPixelPos(u16 *outX, u16 *outY);

#endif
