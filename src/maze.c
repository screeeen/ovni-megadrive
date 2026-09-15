#include "maze.h"
#include "resources.h"

// mazeTiles.png is a 592x16 source image: 37 logical 16x16 cells in a row
// -- cell 0 = floor; cells 1-36 = the dither wall variants from ovni's
// image_edit.png (matching the original's random getRandomValue() 2-10
// look), repeated once per section hue (spec §18): hue h's 9 variants
// live at cells h*9+1 .. h*9+9, same dither shapes every time, just the
// accent color swapped (violet/teal/orange/pink) -- only the background
// color (0x252525) and hue accent are used across all of them, keeping
// every hue a strict duotone. There used to be a 38th "locked door" cell
// (an hourglass/X shape, spec §16) but it was removed (spec §19, user
// feedback): a sealed door now renders as an ordinary wall cell from the
// room's own hue block -- randomWallVariant() picks it the same as any
// other wall cell, no separate value or art needed, so a locked door
// looks exactly like the rest of that room's walls. Rescomp slices it
// into 8x8 VDP tiles in raster order (TILESET ... NONE NONE ROW keeps
// that order untouched, no dedup):
//   row0 (y0-7):  cell0.TL cell0.TR cell1.TL cell1.TR cell2.TL cell2.TR ...
//   row1 (y8-15): cell0.BL cell0.BR cell1.BL cell1.BR cell2.BL cell2.BR ...
// so for a cell value c, its four subtiles are at BASE+2c, BASE+2c+1 (top)
// and BASE+74+2c, BASE+75+2c (bottom).
#define BASE_TILE       TILE_USER_INDEX
#define CELL_ROW_TILES  74 // (592px / 8px) tiles per 8px-tall row of the atlas

#define PATH 0
#define WALL_VARIANTS 9 // 9 dither patterns per hue

static u8 grid[MAZE_H][MAZE_W];

// Offset into the current hue's 9-cell block (spec §18): 0, 9, 18 or 27,
// set once at the top of Maze_generate()/Maze_generateRoom() and read by
// every randomWallVariant() call for the rest of that room's carve.
static u8 wallHueBase;

static const s16 startX = 2;
static const s16 startY = 2;
static const s16 endX = MAZE_W - 1;
static const s16 endY = MAZE_H - 1;

static const s8 dirX[4] = {  0, 0, -2, 2 };
static const s8 dirY[4] = { -2, 2,  0, 0 };

// carve()'s "always descend here regardless of the walls>=2 heuristic"
// targets. The single-room Maze_generate() below sets this to the one
// hardcoded {endX,endY} pair (original js13k behavior, byte-for-byte);
// Maze_generateRoom() sets it to one entry per active door instead.
#define MAX_FORCED_TARGETS 4
static s16 forcedTargetX[MAX_FORCED_TARGETS];
static s16 forcedTargetY[MAX_FORCED_TARGETS];
static u8 forcedTargetCount;

static u8 randomWallVariant(void)
{
    return wallHueBase + 1 + (random() % WALL_VARIANTS);
}

static bool isValid(s16 x, s16 y)
{
    return (x > 0) && (y > 0) && (x < MAZE_W - 1) && (y < MAZE_H - 1) && (grid[y][x] != PATH);
}

static bool isForcedTarget(s16 x, s16 y)
{
    u8 i;

    for (i = 0; i < forcedTargetCount; i++)
        if ((forcedTargetX[i] == x) && (forcedTargetY[i] == y))
            return TRUE;

    return FALSE;
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

            if ((walls >= 2) || isForcedTarget(nx, ny))
                carve(nx, ny);
        }
    }
}

void Maze_generate(void)
{
    s16 x, y;

    wallHueBase = 0; // single-room prototype, spec §18 doesn't apply here -- always the original violet

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            grid[y][x] = randomWallVariant();

    forcedTargetCount = 1;
    forcedTargetX[0] = endX;
    forcedTargetY[0] = endY;

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

// Interior even/even anchor points near each border, on the same step-2
// lattice carve() walks (see docs/spec-mapa-guia.md §5). Doors are punched
// 2 cells wide, bridging the border down to whichever anchor's block was
// force-carved -- same trick as Maze_generate()'s endX/endY punch-through,
// generalized to up to 4 targets instead of 1.
// The room's carve seed doubles as the player's spawn point in the very
// first room of a run (spec §5) -- it's the same (col,row), just named
// differently depending on which role is relevant at the call site.
#define ROOM_SEED_COL MAZE_DOOR_COL
#define ROOM_SEED_ROW MAZE_DOOR_ROW

#define ANCHOR_N_X ROOM_SEED_COL
#define ANCHOR_N_Y 2
#define ANCHOR_S_X ROOM_SEED_COL
#define ANCHOR_S_Y (MAZE_H - 4)
#define ANCHOR_E_X (MAZE_W - 4)
#define ANCHOR_E_Y ROOM_SEED_ROW
#define ANCHOR_W_X 2
#define ANCHOR_W_Y ROOM_SEED_ROW

// carve()'s "walls>=2 OR isForcedTarget" trick only visits a target if the
// DFS's natural wandering happens to reach a cell adjacent to it first --
// rare misses do happen (~0.2% of room/door combinations, spec §11 Paso3).
// Every ANCHOR_* shares an axis with (ROOM_SEED_COL, ROOM_SEED_ROW) by
// construction, so a straight single-width line always reconnects a missed
// target back to the seed, which carve() always visits first.
static void bridgeToSeed(s16 x, s16 y)
{
    s16 cx = x, cy = y;

    while ((cx != ROOM_SEED_COL) || (cy != ROOM_SEED_ROW))
    {
        grid[cy][cx] = PATH;
        if (cx < ROOM_SEED_COL) cx++;
        else if (cx > ROOM_SEED_COL) cx--;
        if (cy < ROOM_SEED_ROW) cy++;
        else if (cy > ROOM_SEED_ROW) cy--;
    }
}

void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        bool lockedN, bool lockedE, bool lockedS, bool lockedW,
                        u8 sectionHue, u16 roomSeed)
{
    s16 x, y;

    wallHueBase = sectionHue * WALL_VARIANTS; // spec §18: every wall cell in this room comes from that hue's block
    setRandomSeed(roomSeed);

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            grid[y][x] = randomWallVariant();

    forcedTargetCount = 0;
    if (doorN) { forcedTargetX[forcedTargetCount] = ANCHOR_N_X; forcedTargetY[forcedTargetCount] = ANCHOR_N_Y; forcedTargetCount++; }
    if (doorE) { forcedTargetX[forcedTargetCount] = ANCHOR_E_X; forcedTargetY[forcedTargetCount] = ANCHOR_E_Y; forcedTargetCount++; }
    if (doorS) { forcedTargetX[forcedTargetCount] = ANCHOR_S_X; forcedTargetY[forcedTargetCount] = ANCHOR_S_Y; forcedTargetCount++; }
    if (doorW) { forcedTargetX[forcedTargetCount] = ANCHOR_W_X; forcedTargetY[forcedTargetCount] = ANCHOR_W_Y; forcedTargetCount++; }

    carve(ROOM_SEED_COL, ROOM_SEED_ROW);

    if (doorN && (grid[ANCHOR_N_Y][ANCHOR_N_X] != PATH)) bridgeToSeed(ANCHOR_N_X, ANCHOR_N_Y);
    if (doorE && (grid[ANCHOR_E_Y][ANCHOR_E_X] != PATH)) bridgeToSeed(ANCHOR_E_X, ANCHOR_E_Y);
    if (doorS && (grid[ANCHOR_S_Y][ANCHOR_S_X] != PATH)) bridgeToSeed(ANCHOR_S_X, ANCHOR_S_Y);
    if (doorW && (grid[ANCHOR_W_Y][ANCHOR_W_X] != PATH)) bridgeToSeed(ANCHOR_W_X, ANCHOR_W_Y);

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

    // A sealed door (spec §16) is punched with independent
    // randomWallVariant() calls per cell instead of PATH -- same as any
    // other wall cell in this room (spec §19), so it blends in with the
    // room's own hue and dither noise instead of standing out as its own
    // fixed-color shape.
    if (doorN)
    {
        grid[0][ANCHOR_N_X] = lockedN ? randomWallVariant() : PATH; grid[0][ANCHOR_N_X + 1] = lockedN ? randomWallVariant() : PATH;
        grid[1][ANCHOR_N_X] = lockedN ? randomWallVariant() : PATH; grid[1][ANCHOR_N_X + 1] = lockedN ? randomWallVariant() : PATH;
    }
    if (doorS)
    {
        grid[MAZE_H - 1][ANCHOR_S_X] = lockedS ? randomWallVariant() : PATH; grid[MAZE_H - 1][ANCHOR_S_X + 1] = lockedS ? randomWallVariant() : PATH;
        grid[MAZE_H - 2][ANCHOR_S_X] = lockedS ? randomWallVariant() : PATH; grid[MAZE_H - 2][ANCHOR_S_X + 1] = lockedS ? randomWallVariant() : PATH;
    }
    if (doorE)
    {
        grid[ANCHOR_E_Y][MAZE_W - 1] = lockedE ? randomWallVariant() : PATH; grid[ANCHOR_E_Y + 1][MAZE_W - 1] = lockedE ? randomWallVariant() : PATH;
        grid[ANCHOR_E_Y][MAZE_W - 2] = lockedE ? randomWallVariant() : PATH; grid[ANCHOR_E_Y + 1][MAZE_W - 2] = lockedE ? randomWallVariant() : PATH;
    }
    if (doorW)
    {
        grid[ANCHOR_W_Y][0] = lockedW ? randomWallVariant() : PATH; grid[ANCHOR_W_Y + 1][0] = lockedW ? randomWallVariant() : PATH;
        grid[ANCHOR_W_Y][1] = lockedW ? randomWallVariant() : PATH; grid[ANCHOR_W_Y + 1][1] = lockedW ? randomWallVariant() : PATH;
    }
}

void Maze_loadGraphics(void)
{
    // index0/1: exact colors from ovni's src/image_edit.png (dither wall
    // art) -- hue 0, unchanged, also what the guide map overlay always
    // uses (spec §18). index2-4: the 3 extra section hues, new palette
    // slots (never touched before), so this can't affect anything that
    // already relied on index0/1 -- text (VDP_drawText's default palette)
    // included. Order/index assignment verified against the compiled
    // out/release/res/resources.s after rebuilding, same as the index0
    // gotcha noted in guidemap.c.
    PAL_setColor(0, RGB24_TO_VDPCOLOR(0x252525));
    PAL_setColor(1, RGB24_TO_VDPCOLOR(0x987DFA)); // hue 0: violet
    PAL_setColor(2, RGB24_TO_VDPCOLOR(0x4AECC4)); // hue 1: teal
    PAL_setColor(3, RGB24_TO_VDPCOLOR(0xFFA53E)); // hue 2: orange
    PAL_setColor(4, RGB24_TO_VDPCOLOR(0xE85D75)); // hue 3: pink

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
