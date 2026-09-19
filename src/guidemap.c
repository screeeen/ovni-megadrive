#include "guidemap.h"
#include "maze.h"
#include "resources.h"
#include "items.h"

typedef struct { u8 col, row; } Coord;

MapCell guideMap[MAX_MAP_ROWS][MAX_MAP_COLS];
u8 startCol, startRow;
u8 insertLinkCol, insertLinkRow, insertLinkDir, insertLinkOffset;
u8 itemCol[ITEM_COUNT], itemRow[ITEM_COUNT];
u8 mapCols = 8, mapRows = 6; // sane default if a caller forgets to set these
u8 itemCount = 5; // sane default if a caller forgets to set this

// Worst case: every CELL_EMPTY cell gets pushed once per already-carved
// neighbor (up to 4 times) before it's claimed. Sized for the largest
// preset (MAX_MAP_COLS/MAX_MAP_ROWS) even when a smaller one is active.
// Reused after generation as scratch space for BFS (queue, then
// candidate list) -- it's otherwise idle once the tree is built.
static Coord frontier[MAX_MAP_COLS * MAX_MAP_ROWS * 4];
static u16 frontierCount;

// dist[][] from the most recent bfsFromStart() call. 0xFF = unreached.
static u8 dist[MAX_MAP_ROWS][MAX_MAP_COLS];

// roomSection[][] from the most recent computeSections() call -- which of
// the (up to MAZE_SECTION_COUNT) branches growing out of the start room
// this room belongs to.
static u8 roomSection[MAX_MAP_ROWS][MAX_MAP_COLS];

static bool inBounds(s16 col, s16 row)
{
    return (col >= 0) && (col < mapCols) && (row >= 0) && (row < mapRows);
}

static bool isEmpty(s16 col, s16 row)
{
    return inBounds(col, row) && (guideMap[row][col].type == CELL_EMPTY);
}

static bool isRoom(s16 col, s16 row)
{
    return inBounds(col, row) && (guideMap[row][col].type == CELL_ROOM);
}

static void pushFrontier(s16 col, s16 row)
{
    if (isEmpty(col, row))
    {
        frontier[frontierCount].col = (u8) col;
        frontier[frontierCount].row = (u8) row;
        frontierCount++;
    }
}

static void pushNeighbors(u8 col, u8 row)
{
    pushFrontier(col - 1, row);
    pushFrontier(col + 1, row);
    pushFrontier(col, row - 1);
    pushFrontier(col, row + 1);
}

static u8 opposite(u8 dir)
{
    return (dir + 2) & 3;
}

static void neighborInDir(u8 col, u8 row, u8 dir, s16 *ncol, s16 *nrow)
{
    switch (dir)
    {
        case DOOR_N: *ncol = col;     *nrow = row - 1; break;
        case DOOR_E: *ncol = col + 1; *nrow = row;      break;
        case DOOR_S: *ncol = col;     *nrow = row + 1; break;
        default:     *ncol = col - 1; *nrow = row;      break; // DOOR_W
    }
}

static void setDoorBit(u8 col, u8 row, u8 dir, bool value)
{
    switch (dir)
    {
        case DOOR_N: guideMap[row][col].doorN = value; break;
        case DOOR_E: guideMap[row][col].doorE = value; break;
        case DOOR_S: guideMap[row][col].doorS = value; break;
        default:     guideMap[row][col].doorW = value; break; // DOOR_W
    }
}

// Valid range for a door's position along its border: a maze column for
// N/S doors, a maze row for E/W doors -- bounded away from the corners.
// Kept 1 cell further in than the absolute minimum a door's own anchor
// geometry needs (maze.h's ANCHOR_DEPTH_N/S/E/W are exactly 2 and
// MAZE_W/H-4): those two constants being fixed, an N/S door's offset
// landing EXACTLY on 2 or MAZE_W-4 puts its own anchor at the identical
// column an E/W door's entire approach corridor always runs along,
// which no amount of chain-routing retry can route around (docs/
// spec-mapa-libre.md §9, maze.h's own comment on the residual this
// causes) -- narrowing the range by 1 on each side removes that exact
// coincidence, though near-misses (a matter of degree, not a hard
// boundary) can still happen and aren't fully eliminated by this alone.
#define DOOR_COL_MIN (2 + 1)
#define DOOR_COL_MAX (MAZE_W - 4 - 1)
#define DOOR_ROW_MIN (2 + 1)
#define DOOR_ROW_MAX (MAZE_H - 4 - 1)

static void setDoorOffset(u8 col, u8 row, u8 dir, u8 offset)
{
    switch (dir)
    {
        case DOOR_N: guideMap[row][col].doorOffsetN = offset; break;
        case DOOR_E: guideMap[row][col].doorOffsetE = offset; break;
        case DOOR_S: guideMap[row][col].doorOffsetS = offset; break;
        default:     guideMap[row][col].doorOffsetW = offset; break; // DOOR_W
    }
}

// Opens the door on both sides of the edge between (col,row) and its
// neighbor in direction dir — a one-way door would strand the player.
// Also rolls where along that shared border it sits -- one random
// value, written to both sides so they always line up exactly.
static void openDoor(u8 col, u8 row, u8 dir)
{
    s16 ncol, nrow;
    u8 offset;

    neighborInDir(col, row, dir, &ncol, &nrow);

    if ((dir == DOOR_N) || (dir == DOOR_S))
        offset = DOOR_COL_MIN + (random() % (DOOR_COL_MAX - DOOR_COL_MIN + 1));
    else
        offset = DOOR_ROW_MIN + (random() % (DOOR_ROW_MAX - DOOR_ROW_MIN + 1));

    setDoorBit(col, row, dir, TRUE);
    setDoorBit((u8) ncol, (u8) nrow, opposite(dir), TRUE);
    setDoorOffset(col, row, dir, offset);
    setDoorOffset((u8) ncol, (u8) nrow, opposite(dir), offset);
}

static void clearMap(void)
{
    s16 col, row;

    for (row = 0; row < mapRows; row++)
    {
        for (col = 0; col < mapCols; col++)
        {
            guideMap[row][col].type    = CELL_EMPTY;
            guideMap[row][col].doorN   = FALSE;
            guideMap[row][col].doorE   = FALSE;
            guideMap[row][col].doorS   = FALSE;
            guideMap[row][col].doorW   = FALSE;
            guideMap[row][col].visited = FALSE;
        }
    }
}

static void carveTree(void)
{
    frontierCount = 0;

    guideMap[startRow][startCol].type = CELL_ROOM;
    pushNeighbors(startCol, startRow);

    while (frontierCount > 0)
    {
        const u16 pick = random() % frontierCount;
        const Coord c = frontier[pick];
        u8 dirs[4];
        u8 n = 0;
        u8 dir;

        frontier[pick] = frontier[--frontierCount];

        if (guideMap[c.row][c.col].type != CELL_EMPTY)
            continue; // claimed by another branch while it waited in the frontier

        if (isRoom(c.col, c.row - 1)) dirs[n++] = DOOR_N;
        if (isRoom(c.col + 1, c.row)) dirs[n++] = DOOR_E;
        if (isRoom(c.col, c.row + 1)) dirs[n++] = DOOR_S;
        if (isRoom(c.col - 1, c.row)) dirs[n++] = DOOR_W;

        dir = dirs[random() % n];

        guideMap[c.row][c.col].type = CELL_ROOM;
        openDoor(c.col, c.row, dir);

        pushNeighbors(c.col, c.row);
    }
}

static u8 doorCountAt(u8 col, u8 row)
{
    u8 n = 0;

    if (guideMap[row][col].doorN) n++;
    if (guideMap[row][col].doorE) n++;
    if (guideMap[row][col].doorS) n++;
    if (guideMap[row][col].doorW) n++;

    return n;
}

// Reverts a leaf room to CELL_EMPTY, closing its single door on both sides.
static void pruneLeaf(u8 col, u8 row)
{
    u8 dir;

    for (dir = DOOR_N; dir <= DOOR_W; dir++)
    {
        if (GuideMap_hasDoor(col, row, dir))
        {
            s16 ncol, nrow;

            neighborInDir(col, row, dir, &ncol, &nrow);
            setDoorBit(col, row, dir, FALSE);
            setDoorBit((u8) ncol, (u8) nrow, opposite(dir), FALSE);
        }
    }

    guideMap[row][col].type = CELL_EMPTY;
}

// Single pass: only cells that are already leaves (1 door) at the time
// they're visited are candidates -- never the start room, never a room
// that only became a leaf because an earlier cell in this same pass was
// pruned.
static void pruneLeaves(void)
{
    s16 col, row;

    for (row = 0; row < mapRows; row++)
    {
        for (col = 0; col < mapCols; col++)
        {
            if ((col == startCol) && (row == startRow))
                continue;

            if ((guideMap[row][col].type == CELL_ROOM) &&
                (doorCountAt((u8) col, (u8) row) == 1) &&
                ((random() % 100) < PRUNE_CHANCE_PERCENT))
            {
                pruneLeaf((u8) col, (u8) row);
            }
        }
    }
}

// BFS from an arbitrary room over the door graph. Fills `dist` (0xFF =
// unreached). Reuses `frontier` as the queue -- the tree carve is done
// with it by this point.
static void bfsFromRoom(u8 fromCol, u8 fromRow)
{
    u16 qHead = 0, qTail = 0;
    s16 col, row;

    for (row = 0; row < mapRows; row++)
        for (col = 0; col < mapCols; col++)
            dist[row][col] = 0xFF;

    dist[fromRow][fromCol] = 0;
    frontier[qTail].col = fromCol;
    frontier[qTail].row = fromRow;
    qTail++;

    while (qHead < qTail)
    {
        const Coord cur = frontier[qHead++];
        const u8 d = dist[cur.row][cur.col];

        if (GuideMap_hasDoor(cur.col, cur.row, DOOR_N) && (dist[cur.row - 1][cur.col] == 0xFF))
        {
            dist[cur.row - 1][cur.col] = d + 1;
            frontier[qTail].col = cur.col; frontier[qTail].row = cur.row - 1; qTail++;
        }
        if (GuideMap_hasDoor(cur.col, cur.row, DOOR_E) && (dist[cur.row][cur.col + 1] == 0xFF))
        {
            dist[cur.row][cur.col + 1] = d + 1;
            frontier[qTail].col = cur.col + 1; frontier[qTail].row = cur.row; qTail++;
        }
        if (GuideMap_hasDoor(cur.col, cur.row, DOOR_S) && (dist[cur.row + 1][cur.col] == 0xFF))
        {
            dist[cur.row + 1][cur.col] = d + 1;
            frontier[qTail].col = cur.col; frontier[qTail].row = cur.row + 1; qTail++;
        }
        if (GuideMap_hasDoor(cur.col, cur.row, DOOR_W) && (dist[cur.row][cur.col - 1] == 0xFF))
        {
            dist[cur.row][cur.col - 1] = d + 1;
            frontier[qTail].col = cur.col - 1; frontier[qTail].row = cur.row; qTail++;
        }
    }
}

static void bfsFromStart(void)
{
    bfsFromRoom(startCol, startRow);
}

// Picks itemCol[]/itemRow[]: itemCount dead-end rooms (exactly 1 door,
// never the start room), spread apart from each other via greedy
// farthest-point sampling -- each pick maximizes the MINIMUM door-graph
// distance to every room already picked (seeded with distance-from-start
// so item 0 isn't just an arbitrary first leaf). Needs one bfsFromRoom()
// call per already-picked item (cheap: <=itemCount passes over a map of
// at most MAX_MAP_COLS*MAX_MAP_ROWS cells).
static void selectItemRooms(void)
{
    static Coord candidates[MAX_MAP_COLS * MAX_MAP_ROWS];
    static u16 minDistToChosen[MAX_MAP_COLS * MAX_MAP_ROWS];
    u16 candidateCount = 0;
    bool startUsedAsFallback = FALSE;
    s16 col, row;
    u8 n;
    u16 i;

    for (row = 0; row < mapRows; row++)
    {
        for (col = 0; col < mapCols; col++)
        {
            if ((guideMap[row][col].type == CELL_ROOM) &&
                (doorCountAt((u8) col, (u8) row) == 1) &&
                !((col == startCol) && (row == startRow)))
            {
                candidates[candidateCount].col = (u8) col;
                candidates[candidateCount].row = (u8) row;
                candidateCount++;
            }
        }
    }

    bfsFromStart();
    for (i = 0; i < candidateCount; i++)
        minDistToChosen[i] = dist[candidates[i].row][candidates[i].col];

    for (n = 0; n < itemCount; n++)
    {
        u16 bestIdx = 0;
        u16 bestScore = 0;

        if (candidateCount == 0)
        {
            // Not enough dead-end rooms for the requested itemCount
            // (small/heavily-pruned map) -- the start room is the only
            // fallback, but it can only stand in for ONE item: a second
            // item placed there too would be an undetectable duplicate
            // (findItemAt only ever returns the FIRST match at a given
            // position), silently stranding whichever later letter
            // never gets its own distinct room. So: use the start room
            // for at most one item, and if the map is too small even
            // for that, gracefully lower itemCount for the rest of THIS
            // game instead -- better to offer fewer letters than the
            // preset nominally asked for than to ship an uncollectible
            // one.
            if (startUsedAsFallback)
            {
                itemCount = n;
                break;
            }
            itemCol[n] = startCol;
            itemRow[n] = startRow;
            startUsedAsFallback = TRUE;
            continue;
        }

        for (i = 0; i < candidateCount; i++)
        {
            if (minDistToChosen[i] >= bestScore)
            {
                bestScore = minDistToChosen[i];
                bestIdx = i;
            }
        }

        itemCol[n] = candidates[bestIdx].col;
        itemRow[n] = candidates[bestIdx].row;

        bfsFromRoom(itemCol[n], itemRow[n]);
        for (i = 0; i < candidateCount; i++)
        {
            const u8 d = dist[candidates[i].row][candidates[i].col];

            if (d < minDistToChosen[i])
                minDistToChosen[i] = d;
        }

        // Remove the just-picked candidate outright -- a candidate's
        // score can only ever decrease afterwards (its distance to
        // itself is always 0), so once every remaining candidate has
        // also degraded to score 0 (easy to reach on a small map with
        // few dead ends relative to itemCount) the >= comparison above
        // would otherwise re-select this exact same room on a later
        // round (a tie at score 0), producing a duplicate itemCol/
        // itemRow entry that silently strands whichever later letter
        // never actually gets its own room. Swap-remove (order doesn't
        // matter here), same pattern carveTree's frontier consumption
        // already uses.
        candidateCount--;
        candidates[bestIdx] = candidates[candidateCount];
        minDistToChosen[bestIdx] = minDistToChosen[candidateCount];
    }
}

// Flood-fills `section` over the subtree rooted at (col,row), excluding
// the edge back to parentDir.
static void floodSection(u8 col, u8 row, s8 parentDir, u8 section)
{
    u8 d;

    roomSection[row][col] = section;

    for (d = 0; d < 4; d++)
    {
        s16 ncol, nrow;

        if (((s8) d) == parentDir) continue;
        if (!GuideMap_hasDoor(col, row, d)) continue;

        neighborInDir(col, row, d, &ncol, &nrow);
        floodSection((u8) ncol, (u8) nrow, (s8) opposite(d), section);
    }
}

// Assigns a section to every room: one per branch growing directly out
// of the start room (N/E/S/W scan order), and everything hanging off
// that branch inherits the same one. A room has at most 4 doors, so
// there are never more than MAZE_SECTION_COUNT branches to number --
// no cycling ever actually needed. The start room itself gets section
// 0, same as whichever branch (if any) got that number too; purely
// structural (depends only on final door topology), computed once.
static void computeSections(void)
{
    u8 section = 0;
    u8 d;

    roomSection[startRow][startCol] = 0;

    for (d = 0; d < 4; d++)
    {
        s16 ncol, nrow;

        if (!GuideMap_hasDoor(startCol, startRow, d)) continue;

        neighborInDir(startCol, startRow, d, &ncol, &nrow);
        floodSection((u8) ncol, (u8) nrow, (s8) opposite(d), section);
        if (section < (MAZE_SECTION_COUNT - 1)) section++;
    }
}

u8 GuideMap_roomSection(u8 col, u8 row)
{
    return roomSection[row][col];
}

typedef struct { u8 col, row, dir; } LinkCandidate;

// Picks (insertLinkCol,insertLinkRow,insertLinkDir): a candidate is
// (room, side) where that side has no real tree door yet. Two tiers,
// tried in order:
//   1. any room whose free side is also a genuine grid perimeter side
//      (keeps the "arrives from outside the map" look when possible).
//   2. any room with any free side at all (covers maps where the tree
//      never happens to touch the grid's outer border on a free side).
// Unlike the old locked-progression design, there's no restriction to
// "a room that's guaranteed to stay unlocked" any more (docs/spec-mapa-
// libre.md §3) -- every room in the tree is always reachable from start
// forever, so ANY room with a free side qualifies. Reuses
// `frontier`-sized scratch since the tree can include up to every room
// in the grid, worst case.
static void selectInsertionLink(void)
{
    static LinkCandidate candidates[MAX_MAP_COLS * MAX_MAP_ROWS * 4];
    u16 candidateCount = 0;
    s16 col, row;

    for (row = 0; row < mapRows; row++)
    {
        for (col = 0; col < mapCols; col++)
        {
            if (guideMap[row][col].type != CELL_ROOM)
                continue;

            if (row == 0)           { candidates[candidateCount].col = (u8) col; candidates[candidateCount].row = (u8) row; candidates[candidateCount].dir = DOOR_N; candidateCount++; }
            if (row == mapRows - 1) { candidates[candidateCount].col = (u8) col; candidates[candidateCount].row = (u8) row; candidates[candidateCount].dir = DOOR_S; candidateCount++; }
            if (col == 0)           { candidates[candidateCount].col = (u8) col; candidates[candidateCount].row = (u8) row; candidates[candidateCount].dir = DOOR_W; candidateCount++; }
            if (col == mapCols - 1) { candidates[candidateCount].col = (u8) col; candidates[candidateCount].row = (u8) row; candidates[candidateCount].dir = DOOR_E; candidateCount++; }
        }
    }

    if (candidateCount == 0)
    {
        // Tier 2: any room in the tree, any side without a real TREE
        // door yet, not limited to the grid's outer border -- the
        // adjacent grid cell in that direction might independently be a
        // real CELL_ROOM too (just never connected here by Prim's, e.g.
        // reached via a different edge), and that's fine: main.c never
        // actually tries to load that neighbor through this side, only
        // through its own real tree door if it has one elsewhere. All
        // that matters is this room's own doorN/E/S/W bit stays FALSE
        // here, so merging in insertLinkDir later can't collide with an
        // existing real door.
        for (row = 0; row < mapRows; row++)
        {
            for (col = 0; col < mapCols; col++)
            {
                if (guideMap[row][col].type != CELL_ROOM)
                    continue;

                if (!GuideMap_hasDoor((u8) col, (u8) row, DOOR_N)) { candidates[candidateCount].col = (u8) col; candidates[candidateCount].row = (u8) row; candidates[candidateCount].dir = DOOR_N; candidateCount++; }
                if (!GuideMap_hasDoor((u8) col, (u8) row, DOOR_E)) { candidates[candidateCount].col = (u8) col; candidates[candidateCount].row = (u8) row; candidates[candidateCount].dir = DOOR_E; candidateCount++; }
                if (!GuideMap_hasDoor((u8) col, (u8) row, DOOR_S)) { candidates[candidateCount].col = (u8) col; candidates[candidateCount].row = (u8) row; candidates[candidateCount].dir = DOOR_S; candidateCount++; }
                if (!GuideMap_hasDoor((u8) col, (u8) row, DOOR_W)) { candidates[candidateCount].col = (u8) col; candidates[candidateCount].row = (u8) row; candidates[candidateCount].dir = DOOR_W; candidateCount++; }
            }
        }
    }

    if (candidateCount == 0)
    {
        // Ultimate defensive fallback -- should be unreachable: every
        // room in the tree has a real tree door (at least the one
        // toward its parent), so it can only run out of free sides if
        // it already has all 4, on every single room, at once. Not
        // expected to trigger at the grid sizes in play.
        insertLinkCol = startCol;
        insertLinkRow = startRow;
        insertLinkDir = DOOR_N;
        insertLinkOffset = DOOR_COL_MIN;
        return;
    }

    {
        const LinkCandidate c = candidates[random() % candidateCount];

        insertLinkCol = c.col;
        insertLinkRow = c.row;
        insertLinkDir = c.dir;

        // Where along that border the opening sits -- same range
        // convention openDoor() uses for a normal tree door, since this
        // behaves like one physically (main.c reuses this exact value
        // for the insertion room's own door too).
        if ((insertLinkDir == DOOR_N) || (insertLinkDir == DOOR_S))
            insertLinkOffset = DOOR_COL_MIN + (random() % (DOOR_COL_MAX - DOOR_COL_MIN + 1));
        else
            insertLinkOffset = DOOR_ROW_MIN + (random() % (DOOR_ROW_MAX - DOOR_ROW_MIN + 1));
    }
}

void GuideMap_generate(void)
{
    clearMap();

    startCol = random() % mapCols;
    startRow = random() % mapRows;

    carveTree();
    pruneLeaves();
    computeSections();
    selectItemRooms();
    selectInsertionLink();
}

u16 GuideMap_roomSeedFor(u16 mapSeed, u8 col, u8 row)
{
    u32 h = mapSeed;

    h = h * 374761393u + col;
    h = h * 668265263u + row;
    h ^= h >> 15;

    return (u16) h;
}

void GuideMap_verifyAllRooms(u16 mapSeed)
{
    s16 col, row;

    for (row = 0; row < mapRows; row++)
    {
        for (col = 0; col < mapCols; col++)
        {
            const MapCell cell = guideMap[row][col];
            const bool isInsertLinkRoom = ((u8) col == insertLinkCol) && ((u8) row == insertLinkRow);
            const bool doorN = cell.doorN || (isInsertLinkRoom && (insertLinkDir == DOOR_N));
            const bool doorE = cell.doorE || (isInsertLinkRoom && (insertLinkDir == DOOR_E));
            const bool doorS = cell.doorS || (isInsertLinkRoom && (insertLinkDir == DOOR_S));
            const bool doorW = cell.doorW || (isInsertLinkRoom && (insertLinkDir == DOOR_W));
            u8 doorOffsets[4];

            if (cell.type != CELL_ROOM)
                continue;

            doorOffsets[DOOR_N] = isInsertLinkRoom && (insertLinkDir == DOOR_N) ? insertLinkOffset : cell.doorOffsetN;
            doorOffsets[DOOR_E] = isInsertLinkRoom && (insertLinkDir == DOOR_E) ? insertLinkOffset : cell.doorOffsetE;
            doorOffsets[DOOR_S] = isInsertLinkRoom && (insertLinkDir == DOOR_S) ? insertLinkOffset : cell.doorOffsetS;
            doorOffsets[DOOR_W] = isInsertLinkRoom && (insertLinkDir == DOOR_W) ? insertLinkOffset : cell.doorOffsetW;

            guideMap[row][col].seedAttempt = Maze_findRoomAttempt(
                doorN, doorE, doorS, doorW, doorOffsets,
                GuideMap_roomSeedFor(mapSeed, (u8) col, (u8) row),
                GuideMap_hasItemAt((u8) col, (u8) row));
        }
    }
}

// 320x224 screen = 40x28 tiles; center the (small) guide map within it.
#define TEXT_COLS 40
#define TEXT_ROWS 28

// Overlay tileset lives right after maze.c's tiles in VRAM (MAZE_TILE_COUNT
// reservation).
// Tile order here must match map_tiles.png left-to-right: OPEN first so the
// image's very first (top-left) pixel is background, same as
// maze_tiles.png's cell0/floor -- rescomp assigns palette index 0 to
// whichever color it scans first, and index0 must be background for this
// tileset to share PAL0 correctly with maze.c's tiles.
#define MAP_TILE_BASE       (TILE_USER_INDEX + MAZE_TILE_COUNT)
#define MAP_TILE_OPEN       (MAP_TILE_BASE + 0) // unused directly; anchors index0=bg (see above)
#define MAP_TILE_CORNER_TL  (MAP_TILE_BASE + 1)
#define MAP_TILE_EDGE_T     (MAP_TILE_BASE + 2)
#define MAP_TILE_CORNER_TR  (MAP_TILE_BASE + 3)
#define MAP_TILE_CORNER_BL  (MAP_TILE_BASE + 4)
#define MAP_TILE_EDGE_B     (MAP_TILE_BASE + 5)
#define MAP_TILE_CORNER_BR  (MAP_TILE_BASE + 6)
#define MAP_TILE_CORRIDOR_H (MAP_TILE_BASE + 7)
#define MAP_TILE_CORRIDOR_V (MAP_TILE_BASE + 8)
#define MAP_TILE_FILL       (MAP_TILE_BASE + 9)

// Each room is a small 3x2 box outlined with a 2px border (hollow center).
// Every room uses the same single color (PAL0's violet) -- the current
// room is marked by SHAPE instead: a solid fill (MAP_TILE_FILL) instead
// of the hollow border. +1 tile of gap between boxes is where a corridor
// segment gets drawn when both sides are visited.
#define ROOM_BOX_W 3
#define ROOM_BOX_H 2
#define ROOM_STRIDE_W (ROOM_BOX_W + 1)
#define ROOM_STRIDE_H (ROOM_BOX_H + 1)

void GuideMap_loadGraphics(void)
{
    // PAL0 is already set up by Maze_loadGraphics (index0=dark bg,
    // index1=violet, 0x987DFA) -- every room and corridor reuses it
    // unchanged, no separate colors for visited/unvisited/current.
    VDP_loadTileSet(&mapTiles, MAP_TILE_BASE, DMA);
}

static void putTile(u16 tileIndex, u16 pal, u16 x, u16 y)
{
    VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(pal, 0, FALSE, FALSE, tileIndex), x, y);
}

// Top-left corner of (col,row)'s 3x2 room box, in BG_A tile units --
// single source of truth for the centering math, shared by
// GuideMap_drawOverlay's two passes and GuideMap_roomBoxPixelPos below.
static void roomBoxOriginTiles(u8 col, u8 row, u16 *outRx, u16 *outRy)
{
    const u16 totalW = (mapCols * ROOM_STRIDE_W) - 1;
    const u16 totalH = (mapRows * ROOM_STRIDE_H) - 1;
    const u16 offsetX = (TEXT_COLS - totalW) / 2;
    const u16 offsetY = (TEXT_ROWS - totalH) / 2;

    *outRx = offsetX + (col * ROOM_STRIDE_W);
    *outRy = offsetY + (row * ROOM_STRIDE_H);
}

// Pixel position of (col,row)'s room box top-left on the guide-map
// overlay -- main.c uses this to place the ship sprite over the current
// room instead of drawing new map art for it. BG_A tiles are 8x8px, so
// this is just roomBoxOriginTiles() scaled up.
void GuideMap_roomBoxPixelPos(u8 col, u8 row, u16 *outX, u16 *outY)
{
    u16 rx, ry;

    roomBoxOriginTiles(col, row, &rx, &ry);
    *outX = rx * 8;
    *outY = ry * 8;
}

void GuideMap_drawOverlay(void)
{
    s16 col, row;

    VDP_clearPlane(BG_A, TRUE);

    for (row = 0; row < mapRows; row++)
    {
        for (col = 0; col < mapCols; col++)
        {
            const MapCell cell = guideMap[row][col];
            const u16 pal = PAL0; // only the tilemap's own violet (0x987DFA), no other colors
            u16 rx, ry;

            roomBoxOriginTiles((u8) col, (u8) row, &rx, &ry);

            if (cell.type != CELL_ROOM)
                continue; // not a room at all -- nothing drawn here

            if (!cell.visited)
                continue; // fog of war: never-visited rooms are fully
                          // hidden -- box, corridors and letter alike --
                          // not just shown differently. The itemCount
                          // item rooms are the one exception, drawn in a
                          // separate pass below regardless of visited
                          // state.

            // Every visited room (current or not) is fully solid -- the
            // blinking white ship sprite (main.c) is what marks the
            // current room now, so the box itself doesn't need to
            // distinguish "current" from "visited" by shape.
            {
                s16 x, y;

                for (y = 0; y < ROOM_BOX_H; y++)
                    for (x = 0; x < ROOM_BOX_W; x++)
                        putTile(MAP_TILE_FILL, pal, rx + x, ry + y);
            }

            // Corridors only between two VISITED rooms -- a stub
            // pointing into an unvisited neighbor would give away its
            // existence/position through the fog, which is exactly what's
            // being hidden.
            if (cell.doorE && (col + 1 < mapCols) && guideMap[row][col + 1].visited)
                putTile(MAP_TILE_CORRIDOR_H, PAL0, rx + ROOM_BOX_W, ry);
            if (cell.doorS && (row + 1 < mapRows) && guideMap[row + 1][col].visited)
                putTile(MAP_TILE_CORRIDOR_V, PAL0, rx + (ROOM_BOX_W / 2), ry + ROOM_BOX_H);

            // Item letter: also gated on cell.visited now (guaranteed
            // true here by the `continue` above) -- letters don't act as
            // a beacon through the fog, discovering one is part of
            // exploring. Docs/spec-mapa-libre.md §6: shown regardless of
            // collection order now, since there's no "due next" concept
            // any more -- just visited or not.
            {
                char letter;

                if (Items_letterAt((u8) col, (u8) row, &letter))
                {
                    char s[2];

                    s[0] = letter;
                    s[1] = '\0';
                    VDP_drawText(s, rx + (ROOM_BOX_W / 2), ry);
                }
            }
        }
    }

    // All itemCount item rooms: regardless of visited state, show that
    // room's letter AND a hollow-border box -- the "known, unvisited"
    // look, scoped to only these itemCount rooms. A room already drawn
    // by the main pass above (visited, including the current room) is
    // skipped here -- it already shows its letter and keeps its normal
    // solid look.
    {
        u8 n;

        for (n = 0; n < itemCount; n++)
        {
            const u8 icol = itemCol[n];
            const u8 irow = itemRow[n];
            u16 rx, ry;
            char s[2];

            if (guideMap[irow][icol].visited)
                continue;

            roomBoxOriginTiles(icol, irow, &rx, &ry);

            putTile(MAP_TILE_CORNER_TL, PAL0, rx,     ry);
            putTile(MAP_TILE_EDGE_T,    PAL0, rx + 1, ry);
            putTile(MAP_TILE_CORNER_TR, PAL0, rx + 2, ry);
            putTile(MAP_TILE_CORNER_BL, PAL0, rx,     ry + 1);
            putTile(MAP_TILE_EDGE_B,    PAL0, rx + 1, ry + 1);
            putTile(MAP_TILE_CORNER_BR, PAL0, rx + 2, ry + 1);

            s[0] = (char) ('A' + n);
            s[1] = '\0';
            VDP_drawText(s, rx + (ROOM_BOX_W / 2), ry);
        }
    }

    // Insertion/extraction room: always drawn (the player has
    // definitely "visited" it, every run starts there), just outside
    // the grid on insertLinkDir's side of (insertLinkCol,insertLinkRow),
    // the same spatial relationship the real door already has to it
    // (maze.c/main.c). Solid fill, same look as any other visited room
    // -- it's not a special place, just physically outside the room tree.
    {
        s16 insCol = insertLinkCol, insRow = insertLinkRow;
        s16 rx, ry;
        u16 linkRx, linkRy;

        switch (insertLinkDir)
        {
            case DOOR_N: insRow--; break;
            case DOOR_S: insRow++; break;
            case DOOR_E: insCol++; break;
            default:     insCol--; break; // DOOR_W
        }

        {
            const s16 totalW = (mapCols * ROOM_STRIDE_W) - 1;
            const s16 totalH = (mapRows * ROOM_STRIDE_H) - 1;
            const s16 offsetX = (TEXT_COLS - totalW) / 2;
            const s16 offsetY = (TEXT_ROWS - totalH) / 2;

            rx = offsetX + (insCol * ROOM_STRIDE_W);
            ry = offsetY + (insRow * ROOM_STRIDE_H);
        }

        // Clamp fully on-screen: exact adjacency to the grid isn't
        // always possible for the biggest presets (10x8 leaves almost
        // no margin around the grid itself) -- degrades to "as close as
        // fits" instead of drawing off-plane.
        if (rx < 0) rx = 0;
        if (rx > (TEXT_COLS - ROOM_BOX_W)) rx = TEXT_COLS - ROOM_BOX_W;
        if (ry < 0) ry = 0;
        if (ry > (TEXT_ROWS - ROOM_BOX_H)) ry = TEXT_ROWS - ROOM_BOX_H;

        {
            s16 x, y;

            for (y = 0; y < ROOM_BOX_H; y++)
                for (x = 0; x < ROOM_BOX_W; x++)
                    putTile(MAP_TILE_FILL, PAL0, (u16) (rx + x), (u16) (ry + y));
        }

        // Corridor stub from the periphery room's own side, toward the
        // insertion room -- same single-tile-gap convention the main
        // per-room loop above uses for real inter-room corridors.
        // Bounds-checked (unlike the main loop's, which never needs it --
        // a real neighbor is always inside the grid): the periphery room
        // can sit right at column/row 0 of the widest/tallest presets,
        // where there's no on-screen tile left for a stub on that side.
        roomBoxOriginTiles(insertLinkCol, insertLinkRow, &linkRx, &linkRy);
        switch (insertLinkDir)
        {
            case DOOR_N:
                if (linkRy > 0)
                    putTile(MAP_TILE_CORRIDOR_V, PAL0, linkRx + (ROOM_BOX_W / 2), linkRy - 1);
                break;
            case DOOR_S:
                if ((linkRy + ROOM_BOX_H) < TEXT_ROWS)
                    putTile(MAP_TILE_CORRIDOR_V, PAL0, linkRx + (ROOM_BOX_W / 2), linkRy + ROOM_BOX_H);
                break;
            case DOOR_E:
                if ((linkRx + ROOM_BOX_W) < TEXT_COLS)
                    putTile(MAP_TILE_CORRIDOR_H, PAL0, linkRx + ROOM_BOX_W, linkRy);
                break;
            default: // DOOR_W
                if (linkRx > 0)
                    putTile(MAP_TILE_CORRIDOR_H, PAL0, linkRx - 1, linkRy);
                break;
        }
    }
}

bool GuideMap_hasDoor(u8 col, u8 row, u8 dir)
{
    switch (dir)
    {
        case DOOR_N: return guideMap[row][col].doorN;
        case DOOR_E: return guideMap[row][col].doorE;
        case DOOR_S: return guideMap[row][col].doorS;
        default:     return guideMap[row][col].doorW; // DOOR_W
    }
}

u8 GuideMap_doorOffset(u8 col, u8 row, u8 dir)
{
    switch (dir)
    {
        case DOOR_N: return guideMap[row][col].doorOffsetN;
        case DOOR_E: return guideMap[row][col].doorOffsetE;
        case DOOR_S: return guideMap[row][col].doorOffsetS;
        default:     return guideMap[row][col].doorOffsetW; // DOOR_W
    }
}

bool GuideMap_hasItemAt(u8 col, u8 row)
{
    u8 i;

    for (i = 0; i < itemCount; i++)
        if ((itemCol[i] == col) && (itemRow[i] == row))
            return TRUE;

    return FALSE;
}
