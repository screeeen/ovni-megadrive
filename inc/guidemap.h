#ifndef _GUIDEMAP_H_
#define _GUIDEMAP_H_

#include <genesis.h>

// Largest grid the menu's size presets can select (main.c) -- fixes the
// static array sizes below. The ACTIVE size for the current game is
// (mapCols, mapRows), set by main.c before calling GuideMap_generate();
// every loop/bounds-check in guidemap.c uses those, not these two.
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
    // Which of maze.c's Maze_findRoomAttempt candidates verified clean
    // for this room -- found ONCE by GuideMap_verifyAllRooms, while
    // building the whole map (docs/spec-mapa-libre.md §5), never during
    // real-time play. 3 bits: must stay wide enough for maze.c's own
    // MAZE_MAX_SEED_ATTEMPTS (currently 8, i.e. indices 0-7) -- not
    // exposed as a shared constant since nothing outside maze.c needs
    // the exact bound, only that this field fits it.
    u8 seedAttempt: 3;
    // Where each active door sits along its border: a maze column (for
    // doorN/doorS) or maze row (for doorE/doorW), in MAZE_TILE_PX cell
    // units. Meaningless when the matching doorX above is FALSE. Both
    // rooms sharing an edge always carry the same value for it
    // (openDoor sets both sides together), so a room's own door lines
    // up exactly with its neighbor's matching door.
    u8 doorOffsetN, doorOffsetE, doorOffsetS, doorOffsetW;
} MapCell;

#define DOOR_N 0
#define DOOR_E 1
#define DOOR_S 2
#define DOOR_W 3

// % chance a leaf room (exactly 1 door) reverts to CELL_EMPTY after the
// tree is carved. The start room is never a candidate.
#define PRUNE_CHANCE_PERCENT 25

// Active grid size for the current game, chosen from the menu's presets
// (main.c) -- set BOTH before calling GuideMap_generate(). Must be <=
// MAX_MAP_COLS/MAX_MAP_ROWS.
extern u8 mapCols, mapRows;

extern MapCell guideMap[MAX_MAP_ROWS][MAX_MAP_COLS];
extern u8 startCol, startRow;

// Which perimeter room, and which of its (necessarily neighbor-less)
// border sides, the special insertion room's single door leads into --
// a REAL border opening on that room's insertLinkDir side, not a blind
// teleport into its center. main.c's actual physical starting point for
// the player, distinct from startCol/startRow (which stays the room
// tree's logical root -- section-numbering origin, unrelated to where
// the ship first appears). insertLinkDir is DOOR_N/E/S/W; by
// construction it can never coincide with a real door direction the
// room already has toward a grid neighbor (only sides genuinely missing
// a neighbor are ever offered as candidates), so main.c can always tell
// the two apart unambiguously. ANY room in the tree qualifies now (docs/
// spec-mapa-libre.md §3): there's no lock concept any more that could
// seal one off, so unlike the old locked-progression design, this
// doesn't need to be restricted to some guaranteed-always-open path.
// Where insertLinkDir's border opening sits -- same units/axis
// convention as MapCell's doorOffsetN/E/S/W (a maze column for N/S, a
// maze row for E/W). Shared by both ends of the link: the periphery
// room's own opening on its insertLinkDir side, AND the insertion
// room's single door (main.c passes this same value to
// Maze_generateInsertionRoom too) -- N/S and their opposite S/N share
// the column axis, E/W and their opposite W/E share the row axis, so no
// translation is needed between the two sides, just reuse the one
// value.
extern u8 insertLinkOffset;
extern u8 insertLinkCol, insertLinkRow, insertLinkDir;

// Item rooms: letters A..A+itemCount-1, one per dead-end room (exactly 1
// door), picked by farthest-point sampling so they end up spread apart
// from the start AND from each other, not clustered. itemCol[n]/
// itemRow[n] is where letter n lives; items.c owns which ones are
// collected -- in ANY order now (docs/spec-mapa-libre.md §6), not
// necessarily 0,1,2... ITEM_COUNT is the largest itemCount any preset
// can pick (main.c) -- fixes the static array sizes below, same MAX_/
// active relationship as MAX_MAP_COLS/mapCols above. itemCount itself is
// set by main.c before calling GuideMap_generate().
#define ITEM_COUNT 5
extern u8 itemCount;
extern u8 itemCol[ITEM_COUNT];
extern u8 itemRow[ITEM_COUNT];

// Carves a new room tree via randomized Prim's algorithm, grown from a
// random (startCol, startRow); prunes some leaves; picks
// (insertLinkCol, insertLinkRow, insertLinkDir) among the map's rooms
// and their free border sides; then picks itemCol[]/itemRow[].
// guideMap/startCol/startRow/insertLinkCol/insertLinkRow/insertLinkDir/
// itemCol/itemRow are all valid once this returns, and so is the
// per-room section (see GuideMap_roomSection below) -- it's purely
// structural (final door topology only), so it's computed here and
// never needs recomputing afterwards (docs/spec-mapa-libre.md §3: there
// is no more progress-dependent state -- no locks -- that would ever
// need this map regenerated or reinterpreted mid-run).
void GuideMap_generate(void);

// Deterministic per-room seed: same (col,row) under the same mapSeed
// always yields the same value, so a room's chosen layout persists
// across re-entries without ever storing the grid itself -- only the
// small seedAttempt index (MapCell) needs to be remembered, since
// Maze_generateRoom(...,roomSeed,attempt) reproduces the rest.
u16 GuideMap_roomSeedFor(u16 mapSeed, u8 col, u8 row);

// Finds and stores (MapCell's seedAttempt) which of maze.c's
// Maze_findRoomAttempt candidates verifies clean, for EVERY room in the
// tree -- the "generar primero el mapa" pass (docs/spec-mapa-libre.md
// §2/§5, user request), run once from main.c's newGame() right after
// GuideMap_generate(), before the ship is ever let loose in it. Costs
// one Maze_findRoomAttempt call (up to MAZE_MAX_SEED_ATTEMPTS carve+
// verify passes each, cheap individually) per room in the tree -- see
// docs/spec-mapa-libre.md §9 for why this could become perceptible on
// the largest presets, and what to do about it if it is.
void GuideMap_verifyAllRooms(u16 mapSeed);

bool GuideMap_hasDoor(u8 col, u8 row, u8 dir);

// Same value as guideMap[row][col]'s doorOffsetN/E/S/W field for dir --
// a thin accessor so callers outside guidemap.c don't need to know the
// MapCell layout to read it. Meaningless if GuideMap_hasDoor(col,row,dir)
// is FALSE.
u8 GuideMap_doorOffset(u8 col, u8 row, u8 dir);

// TRUE if room (col,row) is one of the itemCount dead-end rooms holding
// a letter, collected or not -- main.c passes this straight into
// Maze_generateRoom's hasItem parameter so its generation-time
// verification also checks the item's own hub is reachable, not just
// the room's single door.
bool GuideMap_hasItemAt(u8 col, u8 row);

// Which branch (0..MAZE_SECTION_COUNT-1, maze.h) growing directly out of
// the start room this room belongs to -- every room hanging off the
// same direct child of the start shares one number, the start room
// itself is section 0. main.c passes this into Maze_generateRoom's
// sectionHue so each branch of the map reads as its own colored zone
// while playing. Valid once GuideMap_generate() returns.
u8 GuideMap_roomSection(u8 col, u8 row);

// Uploads the overlay tileset to VRAM (right after maze.c's tileset --
// see MAZE_TILE_COUNT) and sets up PAL2 for the current-room highlight.
// Call once at boot, alongside Maze_loadGraphics().
void GuideMap_loadGraphics(void);

// Draws the fog-of-war overlay to BG_A, replacing whatever was there
// (the current room's maze) -- caller is responsible for calling
// Maze_draw() again to restore it when the overlay closes. A cell is
// drawn -- box, corridor stubs into it, and any item letter it holds --
// only if visited==TRUE; everything else (never entered) is fully
// omitted, not shown differently. Every visited room, current or not,
// is drawn fully solid -- no longer takes a (curCol,curRow) parameter,
// since marking the current room is main.c's job (the blinking white
// ship sprite, positioned via GuideMap_roomBoxPixelPos), not this
// function's. The itemCount item rooms are the one exception: each
// always gets its letter and a hollow-border box drawn, regardless of
// visited state -- unless it was already drawn by the main pass
// (visited), in which case that normal look (plus its letter, same as
// before) is left alone instead of doubled up. Docs/spec-mapa-libre.md
// §6: there's no more "letter due next" staging -- every item room
// shows its letter as soon as it's either visited or was always shown
// unvisited-with-a-frame, whichever applies; the only ordering left is
// visited-vs-not (fog of war), not collection sequence.
void GuideMap_drawOverlay(void);

// Pixel position (BG_A tile units x8) of (col,row)'s room box top-left on
// the guide-map overlay -- main.c uses this to reposition the ship
// sprite over the current room's box instead of hiding it while the
// overlay is open, so the sprite itself marks the player's position on
// the map (no new art -- the existing 16x16 playerShip sprite is reused
// as-is; it fits within the box's 24x16px footprint with a small
// centering offset the caller applies).
void GuideMap_roomBoxPixelPos(u8 col, u8 row, u16 *outX, u16 *outY);

#endif
