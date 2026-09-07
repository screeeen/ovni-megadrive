#include "maze.h"
#include "resources.h"

// mazeTiles.png is a 160x16 source image: 10 logical 16x16 cells in a row
// (cell 0 = floor, cells 1-9 = the dither wall variants from ovni's
// image_edit.png, matching the original's random getRandomValue() 2-10
// look). Rescomp slices it into 8x8 VDP tiles in raster order (TILESET
// ... NONE NONE ROW keeps that order untouched, no dedup):
//   row0 (y0-7):  cell0.TL cell0.TR cell1.TL cell1.TR cell2.TL cell2.TR ...
//   row1 (y8-15): cell0.BL cell0.BR cell1.BL cell1.BR cell2.BL cell2.BR ...
// so for a cell value c, its four subtiles are at BASE+2c, BASE+2c+1 (top)
// and BASE+20+2c, BASE+21+2c (bottom).
#define BASE_TILE       TILE_USER_INDEX
#define CELL_ROW_TILES  20 // (160px / 8px) tiles per 8px-tall row of the atlas

#define PATH 0
#define WALL_VARIANTS 9 // cells 1..9, one per dither pattern

static u8 grid[MAZE_H][MAZE_W];

static const s16 startX = 2;
static const s16 startY = 2;
static const s16 endX = MAZE_W - 1;
static const s16 endY = MAZE_H - 1;

static const s8 dirX[4] = {  0, 0, -2, 2 };
static const s8 dirY[4] = { -2, 2,  0, 0 };

static u8 randomWallVariant(void)
{
    return 1 + (random() % WALL_VARIANTS);
}

static bool isValid(s16 x, s16 y)
{
    return (x > 0) && (y > 0) && (x < MAZE_W - 1) && (y < MAZE_H - 1) && (grid[y][x] != PATH);
}

static void shuffleDirs(s8 order[4])
{
    for (s16 i = 3; i > 0; i--)
    {
        s16 j = random() % (i + 1);
        s8 tmp = order[i];

        order[i] = order[j];
        order[j] = tmp;
    }
}

// Recursive-backtracker carve, ported from ovni's src/app/maps/generateMap.js
static void carve(s16 x, s16 y)
{
    s8 order[4] = { 0, 1, 2, 3 };
    s16 i;

    for (s16 dx = 0; dx < 2; dx++)
    {
        for (s16 dy = 0; dy < 2; dy++)
        {
            if ((x + dx < MAZE_W) && (y + dy < MAZE_H))
                grid[y + dy][x + dx] = PATH;
        }
    }

    shuffleDirs(order);

    for (i = 0; i < 4; i++)
    {
        s16 dx = dirX[(u16) order[i]];
        s16 dy = dirY[(u16) order[i]];
        s16 nx = x + dx;
        s16 ny = y + dy;

        if (isValid(nx, ny))
        {
            u16 walls = 0;
            s16 j;

            for (j = 0; j < 4; j++)
            {
                s16 ax = nx + dirX[j];
                s16 ay = ny + dirY[j];

                if ((ax > 0) && (ay > 0) && (ax < MAZE_W - 1) && (ay < MAZE_H - 1) && (grid[ay][ax] != PATH))
                    walls++;
            }

            if ((walls >= 2) || ((nx == endX) && (ny == endY)))
                carve(nx, ny);
        }
    }
}

void Maze_generate(void)
{
    s16 x, y;

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            grid[y][x] = randomWallVariant();

    carve(startX, startY);

    for (x = 0; x < MAZE_W; x++)
    {
        grid[0][x] = randomWallVariant();
        grid[MAZE_H - 1][x] = randomWallVariant();
    }
    for (y = 0; y < MAZE_H; y++)
    {
        grid[y][0] = randomWallVariant();
        grid[y][MAZE_W - 1] = randomWallVariant();
    }

    // Guaranteed clear path around the start/end, same hardcoded punch-through
    // as the original generateMap.js.
    grid[startY][startX] = PATH;
    grid[startY - 1][startX + 1] = PATH;
    grid[startY - 1][startX] = PATH;
    grid[startY - 2][startX] = PATH;

    grid[endY][endX - 1] = PATH;
    grid[endY][endX - 2] = PATH;
    grid[endY - 1][endX - 1] = PATH;
    grid[endY - 2][endX - 1] = PATH;
}

void Maze_loadGraphics(void)
{
    // Exact colors from ovni's src/image_edit.png (dither wall art).
    PAL_setColor(0, RGB24_TO_VDPCOLOR(0x252525));
    PAL_setColor(1, RGB24_TO_VDPCOLOR(0x987DFA));

    VDP_loadTileSet(&mazeTiles, BASE_TILE, DMA);
}

void Maze_draw(void)
{
    s16 x, y;

    for (y = 0; y < MAZE_H; y++)
    {
        for (x = 0; x < MAZE_W; x++)
        {
            const u8 c = grid[y][x];
            const u16 tl = BASE_TILE + (2 * c);
            const u16 tr = tl + 1;
            const u16 bl = BASE_TILE + CELL_ROW_TILES + (2 * c);
            const u16 br = bl + 1;
            const u16 tx = x * 2;
            const u16 ty = y * 2;

            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, tl), tx, ty);
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, tr), tx + 1, ty);
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, bl), tx, ty + 1);
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, br), tx + 1, ty + 1);
        }
    }
}

bool Maze_isWall(s16 tx, s16 ty)
{
    if ((tx < 0) || (ty < 0) || (tx >= MAZE_W) || (ty >= MAZE_H))
        return TRUE;

    return grid[ty][tx] != PATH;
}

s16 Maze_startPixelX(void)
{
    return startX * MAZE_TILE_PX;
}

s16 Maze_startPixelY(void)
{
    return (startY - 1) * MAZE_TILE_PX;
}
