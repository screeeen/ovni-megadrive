#include "guidemap.h"
#include "maze.h"
#include "resources.h"

typedef struct { u8 col, row; } Coord;

MapCell guideMap[MAX_MAP_ROWS][MAX_MAP_COLS];
u8 startCol, startRow;
u8 goalCol, goalRow;
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

// BFS from (startCol,startRow) over the door graph. Fills `dist` (0xFF =
// unreached) and returns the eccentricity (max distance found). Reuses
// `frontier` as the queue -- the tree carve is done with it by this point.
static u8 bfsFromStart(void)
{
    u16 qHead = 0, qTail = 0;
    u8 ecc = 0;
    s16 col, row;

    for (row = 0; row < mapRows; row++)
        for (col = 0; col < mapCols; col++)
            dist[row][col] = 0xFF;

    dist[startRow][startCol] = 0;
    frontier[qTail].col = startCol;
    frontier[qTail].row = startRow;
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

void GuideMap_generate(void)
{
    clearMap();

    startCol = random() % mapCols;
    startRow = random() % mapRows;

    carveTree();
    pruneLeaves();
    selectGoal();
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

            if ((col == curCol) && (row == curRow))
            {
                // Current room: fully solid -- shape marks position since
                // color no longer does.
                s16 x, y;

                for (y = 0; y < ROOM_BOX_H; y++)
                    for (x = 0; x < ROOM_BOX_W; x++)
                        putTile(MAP_TILE_FILL, pal, rx + x, ry + y);
            }
            else if (cell.visited)
            {
                // Visited (not current): filled with the maze's own wall
                // dither pattern (MAZE_WALL_DITHER_TILE, maze.h) instead of
                // a flat fill -- a third distinct look between "hollow
                // outline" (known, unvisited) and "fully solid" (current),
                // still one color throughout.
                s16 x, y;

                for (y = 0; y < ROOM_BOX_H; y++)
                    for (x = 0; x < ROOM_BOX_W; x++)
                        putTile(MAZE_WALL_DITHER_TILE, pal, rx + x, ry + y);
            }
            else
            {
                // Known, unvisited: 3x2 outline (top row: corner/edge/corner,
                // bottom row: corner/edge/corner), hollow in the middle.
                putTile(MAP_TILE_CORNER_TL, pal, rx,     ry);
                putTile(MAP_TILE_EDGE_T,    pal, rx + 1, ry);
                putTile(MAP_TILE_CORNER_TR, pal, rx + 2, ry);
                putTile(MAP_TILE_CORNER_BL, pal, rx,     ry + 1);
                putTile(MAP_TILE_EDGE_B,    pal, rx + 1, ry + 1);
                putTile(MAP_TILE_CORNER_BR, pal, rx + 2, ry + 1);
            }

            // Corridors are always shown between two rooms with a door
            // between them (doors only ever exist between two CELL_ROOM
            // cells by construction) -- unvisited rooms are shown too now,
            // so hiding the corridor to one would look inconsistent.
            if (cell.doorE && (col + 1 < mapCols))
                putTile(MAP_TILE_CORRIDOR_H, PAL0, rx + ROOM_BOX_W, ry);
            if (cell.doorS && (row + 1 < mapRows))
                putTile(MAP_TILE_CORRIDOR_V, PAL0, rx + (ROOM_BOX_W / 2), ry + ROOM_BOX_H);
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
