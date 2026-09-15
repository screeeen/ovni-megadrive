#include "guidemap.h"
#include "maze.h"
#include "resources.h"
#include "items.h"

typedef struct { u8 col, row; } Coord;

MapCell guideMap[MAX_MAP_ROWS][MAX_MAP_COLS];
u8 startCol, startRow;
u8 goalCol, goalRow;
u8 itemCol[ITEM_COUNT], itemRow[ITEM_COUNT];
u8 mapCols = 8, mapRows = 6; // sane default if a caller forgets to set these

// Worst case: every CELL_EMPTY cell gets pushed once per already-carved
// neighbor (up to 4 times) before it's claimed. See spec §8. Sized for the
// largest preset (MAX_MAP_COLS/MAX_MAP_ROWS) even when a smaller one is
// active. Reused after generation as scratch space for the start/goal BFS
// (queue, then candidate list) -- it's otherwise idle once the tree is built.
static Coord frontier[MAX_MAP_COLS * MAX_MAP_ROWS * 4];
static u16 frontierCount;

// dist[][] from the most recent bfsFromStart() call. 0xFF = unreached.
static u8 dist[MAX_MAP_ROWS][MAX_MAP_COLS];

// roomLocked[][] from the most recent GuideMap_recomputeLocks() call (spec
// §16). containsUnlockedScratch[][] is scratch space live only during that
// call's own recursion, not meant to be read afterwards.
static bool roomLocked[MAX_MAP_ROWS][MAX_MAP_COLS];
static bool containsUnlockedScratch[MAX_MAP_ROWS][MAX_MAP_COLS];

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

// Opens the door on both sides of the edge between (col,row) and its
// neighbor in direction dir — a one-way door would strand the player.
static void openDoor(u8 col, u8 row, u8 dir)
{
    s16 ncol, nrow;

    neighborInDir(col, row, dir, &ncol, &nrow);
    setDoorBit(col, row, dir, TRUE);
    setDoorBit((u8) ncol, (u8) nrow, opposite(dir), TRUE);
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
// pruned (spec §4.2 talks about "algunas hojas", not cascading removal).
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
// unreached) and returns the eccentricity (max distance found from that
// room). Reuses `frontier` as the queue -- the tree carve is done with it
// by this point.
static u8 bfsFromRoom(u8 fromCol, u8 fromRow)
{
    u16 qHead = 0, qTail = 0;
    u8 ecc = 0;
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

        if (d > ecc) ecc = d;

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

    return ecc;
}

static u8 bfsFromStart(void)
{
    return bfsFromRoom(startCol, startRow);
}

static s16 itemIndexAtRoom(u8 col, u8 row)
{
    u8 i;

    for (i = 0; i < ITEM_COUNT; i++)
        if ((itemCol[i] == col) && (itemRow[i] == row))
            return i;

    return -1;
}

static bool roomHasUnlockedItem(u8 col, u8 row)
{
    const s16 i = itemIndexAtRoom(col, row);

    return (i >= 0) && Items_isUnlocked((u8) i);
}

// Post-order over the room tree (spec §16): does the subtree rooted at
// (col,row) -- excluding the edge back to parentDir, -1 for the root call
// -- contain a room whose item is at or before the one currently due?
// Fills containsUnlockedScratch[][] for every room visited along the way,
// which markLocked() below then reads top-down.
static bool computeContainsUnlocked(u8 col, u8 row, s8 parentDir)
{
    bool result = roomHasUnlockedItem(col, row);
    u8 d;

    for (d = 0; d < 4; d++)
    {
        s16 ncol, nrow;

        if (((s8) d) == parentDir) continue;
        if (!GuideMap_hasDoor(col, row, d)) continue;

        neighborInDir(col, row, d, &ncol, &nrow);
        if (computeContainsUnlocked((u8) ncol, (u8) nrow, (s8) opposite(d)))
            result = TRUE;
    }

    containsUnlockedScratch[row][col] = result;
    return result;
}

// Marks every room in the subtree rooted at (col,row) as locked -- called
// once the parent's side has already decided nothing due lives past this
// edge, so the whole branch behind it seals (not just the door itself).
static void markSubtreeLocked(u8 col, u8 row, s8 parentDir)
{
    u8 d;

    roomLocked[row][col] = TRUE;

    for (d = 0; d < 4; d++)
    {
        s16 ncol, nrow;

        if (((s8) d) == parentDir) continue;
        if (!GuideMap_hasDoor(col, row, d)) continue;

        neighborInDir(col, row, d, &ncol, &nrow);
        markSubtreeLocked((u8) ncol, (u8) nrow, (s8) opposite(d));
    }
}

// Pre-order from the start (already known accessible): for each child
// edge, either its subtree contains something due (recurse, stays open)
// or it doesn't (markSubtreeLocked seals the whole branch) -- this finds
// exactly the edge closest to the start where a locked branch begins.
static void markLocked(u8 col, u8 row, s8 parentDir)
{
    u8 d;

    roomLocked[row][col] = FALSE;

    for (d = 0; d < 4; d++)
    {
        s16 ncol, nrow;

        if (((s8) d) == parentDir) continue;
        if (!GuideMap_hasDoor(col, row, d)) continue;

        neighborInDir(col, row, d, &ncol, &nrow);

        if (containsUnlockedScratch[nrow][ncol])
            markLocked((u8) ncol, (u8) nrow, (s8) opposite(d));
        else
            markSubtreeLocked((u8) ncol, (u8) nrow, (s8) opposite(d));
    }
}

void GuideMap_recomputeLocks(void)
{
    computeContainsUnlocked(startCol, startRow, -1);
    markLocked(startCol, startRow, -1);
}

bool GuideMap_isRoomLocked(u8 col, u8 row)
{
    return roomLocked[row][col];
}

// Picks (goalCol,goalRow) among rooms at >= GOAL_MIN_DISTANCE_PERCENT of the
// start's eccentricity, chosen at random among the qualifying candidates
// (reuses `frontier`, idle after bfsFromStart's queue use) for variety
// across generations rather than always the single farthest room.
static void selectGoal(void)
{
    const u8 ecc = bfsFromStart();
    // Ceiling division: a floored threshold would let through candidates
    // whose real ratio is just under GOAL_MIN_DISTANCE_PERCENT (e.g. ecc=8
    // floors 65% to 5, but 5/8 is only 62.5%).
    const u8 minGoalDist = (u8) ((((u16) ecc * GOAL_MIN_DISTANCE_PERCENT) + 99) / 100);
    u16 candidateCount = 0;
    s16 col, row;

    for (row = 0; row < mapRows; row++)
    {
        for (col = 0; col < mapCols; col++)
        {
            if ((guideMap[row][col].type == CELL_ROOM) &&
                (dist[row][col] != 0xFF) && (dist[row][col] >= minGoalDist))
            {
                frontier[candidateCount].col = (u8) col;
                frontier[candidateCount].row = (u8) row;
                candidateCount++;
            }
        }
    }

    if (candidateCount == 0)
    {
        // Degenerate map (ecc == 0, e.g. every other room got pruned):
        // nothing is farther than the start itself.
        goalCol = startCol;
        goalRow = startRow;
        return;
    }

    {
        const Coord goal = frontier[random() % candidateCount];

        goalCol = goal.col;
        goalRow = goal.row;
    }
}

// Picks itemCol[]/itemRow[] (spec §13): ITEM_COUNT dead-end rooms (exactly
// 1 door, never the start room), spread apart from each other via greedy
// farthest-point sampling -- each pick maximizes the MINIMUM door-graph
// distance to every room already picked (seeded with distance-from-start
// so item 0 isn't just an arbitrary first leaf). Needs one bfsFromRoom()
// call per already-picked item (cheap: <=ITEM_COUNT passes over a map of
// at most MAX_MAP_COLS*MAX_MAP_ROWS cells).
static void selectItemRooms(void)
{
    static Coord candidates[MAX_MAP_COLS * MAX_MAP_ROWS];
    static u16 minDistToChosen[MAX_MAP_COLS * MAX_MAP_ROWS];
    u16 candidateCount = 0;
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

    for (n = 0; n < ITEM_COUNT; n++)
    {
        u16 bestIdx = 0;
        u16 bestScore = 0;

        if (candidateCount == 0)
        {
            // Not enough dead-end rooms (degenerate tiny/heavily-pruned
            // map): fall back to the start room rather than crash. Not
            // expected to trigger at the grid sizes in play (spec §0).
            itemCol[n] = startCol;
            itemRow[n] = startRow;
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
        minDistToChosen[bestIdx] = 0; // never picked again
    }
}

void GuideMap_generate(void)
{
    clearMap();

    startCol = random() % mapCols;
    startRow = random() % mapRows;

    carveTree();
    pruneLeaves();
    selectGoal();
    selectItemRooms();
}

// 320x224 screen = 40x28 tiles; center the (small) guide map within it.
#define TEXT_COLS 40
#define TEXT_ROWS 28

// Overlay tileset lives right after maze.c's tiles in VRAM (spec §5's
// MAZE_TILE_COUNT reservation).
// Tile order here must match map_tiles.png left-to-right: OPEN first so the
// image's very first (top-left) pixel is background, same as
// maze_tiles.png's cell0/floor -- rescomp assigns palette index 0 to
// whichever color it scans first, and index0 must be background for this
// tileset to share PAL0 correctly with maze.c's tiles (verified against the
// compiled bytes in out/release/res/resources.s after a mixup: index0
// ended up meaning "violet" instead when WALL was drawn first).
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

// Each room is a small 3x2 box outlined with a 2px border (hollow center) --
// reverted from a dithered-texture fill (spec §6bis) after the user tried
// it and preferred the bordered look. Every room uses the same single
// color (PAL0's violet, no PAL2/PAL3) -- the current room is marked by
// SHAPE instead: a solid fill (MAP_TILE_FILL) instead of the hollow
// border, since color is off the table. +1 tile of gap between boxes is
// where a corridor segment gets drawn when both sides are visited.
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

void GuideMap_drawOverlay(u8 curCol, u8 curRow)
{
    const u16 totalW = (mapCols * ROOM_STRIDE_W) - 1;
    const u16 totalH = (mapRows * ROOM_STRIDE_H) - 1;
    const u16 offsetX = (TEXT_COLS - totalW) / 2;
    const u16 offsetY = (TEXT_ROWS - totalH) / 2;
    s16 col, row;

    VDP_clearPlane(BG_A, TRUE);

    for (row = 0; row < mapRows; row++)
    {
        for (col = 0; col < mapCols; col++)
        {
            const MapCell cell = guideMap[row][col];
            const u16 rx = offsetX + (col * ROOM_STRIDE_W);
            const u16 ry = offsetY + (row * ROOM_STRIDE_H);
            const u16 pal = PAL0; // only the tilemap's own violet (0x987DFA), no other colors

            if (cell.type != CELL_ROOM)
                continue; // not a room at all -- nothing drawn here

            if (!cell.visited)
                continue; // fog of war (spec §17): never-visited rooms are
                          // fully hidden -- box, corridors and letter alike
                          // -- not just shown differently. A locked room
                          // (spec §16) can never be visited either (the
                          // unlock is monotonic), so this also covers it;
                          // no separate "locked" look is drawn on the map.

            if ((col == curCol) && (row == curRow))
            {
                // Current room: fully solid -- shape marks position since
                // color no longer does.
                s16 x, y;

                for (y = 0; y < ROOM_BOX_H; y++)
                    for (x = 0; x < ROOM_BOX_W; x++)
                        putTile(MAP_TILE_FILL, pal, rx + x, ry + y);
            }
            else
            {
                // Visited (not current): filled with the maze's own wall
                // dither pattern (MAZE_WALL_DITHER_TILE, maze.h) instead of
                // a flat fill -- distinct from "fully solid" (current),
                // still one color throughout.
                s16 x, y;

                for (y = 0; y < ROOM_BOX_H; y++)
                    for (x = 0; x < ROOM_BOX_W; x++)
                        putTile(MAZE_WALL_DITHER_TILE, pal, rx + x, ry + y);
            }

            // Corridors only between two VISITED rooms (spec §17) -- a
            // stub pointing into an unvisited neighbor would give away its
            // existence/position through the fog, which is exactly what's
            // being hidden now.
            if (cell.doorE && (col + 1 < mapCols) && guideMap[row][col + 1].visited)
                putTile(MAP_TILE_CORRIDOR_H, PAL0, rx + ROOM_BOX_W, ry);
            if (cell.doorS && (row + 1 < mapRows) && guideMap[row + 1][col].visited)
                putTile(MAP_TILE_CORRIDOR_V, PAL0, rx + (ROOM_BOX_W / 2), ry + ROOM_BOX_H);

            // Item letter (spec §13/§17): also gated on cell.visited now
            // (guaranteed true here by the `continue` above) -- letters no
            // longer act as a beacon through the fog, discovering one is
            // part of exploring. Only the letters up to the one currently
            // due are revealed (Items_revealedOnMap) once the room itself
            // has been seen. Drawn on top of the box, centered in the
            // middle column (same column the N/S corridor stub would use).
            {
                char letter;

                if (Items_revealedOnMap((u8) col, (u8) row, &letter))
                {
                    char s[2];

                    s[0] = letter;
                    s[1] = '\0';
                    VDP_drawText(s, rx + (ROOM_BOX_W / 2), ry);
                }
            }
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
