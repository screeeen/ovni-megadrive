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

// The room's carve seed doubles as the player's spawn point in the very
// first room of a run (spec §5) -- it's the same (col,row), just named
// differently depending on which role is relevant at the call site.
#define ROOM_SEED_COL MAZE_DOOR_COL
#define ROOM_SEED_ROW MAZE_DOOR_ROW

// Numeric convention matching guidemap.h's DOOR_N/E/S/W (0/1/2/3) --
// defined locally instead of #including guidemap.h, see maze.h's comment
// on Maze_generateRoom/Maze_generateInsertionRoom. Used to index
// doorOffsets[4] and the anchorX/Y[4] scratch arrays below.
#define MAZE_DIR_N 0
#define MAZE_DIR_E 1
#define MAZE_DIR_S 2
#define MAZE_DIR_W 3

// How far into the room from each border a door's anchor point sits
// (spec §5) -- fixed regardless of WHERE along that border the door is
// (spec §30: that's the other axis, doorOffsets[dir]/doorOffset).
#define ANCHOR_DEPTH_N 2
#define ANCHOR_DEPTH_S (MAZE_H - 4)
#define ANCHOR_DEPTH_E (MAZE_W - 4)
#define ANCHOR_DEPTH_W 2

// Combines a direction's fixed depth with its along-border offset (spec
// §30) into the actual (x,y) anchor point carve() targets for that door.
static void anchorForDoor(u8 dir, u8 offset, s16 *outX, s16 *outY)
{
    switch (dir)
    {
        case MAZE_DIR_N: *outX = offset;           *outY = ANCHOR_DEPTH_N; break;
        case MAZE_DIR_S: *outX = offset;           *outY = ANCHOR_DEPTH_S; break;
        case MAZE_DIR_E: *outX = ANCHOR_DEPTH_E;   *outY = offset;         break;
        default:         *outX = ANCHOR_DEPTH_W;   *outY = offset;         break; // MAZE_DIR_W
    }
}

// carve()'s "walls>=2 OR isForcedTarget" trick only visits a target if the
// DFS's natural wandering happens to reach a cell adjacent to it first --
// misses do happen, and more often now that door anchors (spec §30) can
// land anywhere along their border instead of always sharing an axis with
// (ROOM_SEED_COL, ROOM_SEED_ROW). Moves ONE axis at a time -- first
// closes the X gap (marking every intermediate cell along that row),
// then the Y gap (along the resulting column) -- an "L-shaped" path
// where every consecutive pair of marked cells shares a full edge.
//
// BUG FIXED (spec §30bis, found by fuzzing): an earlier version moved
// both axes in the same step whenever they both still differed --
// producing a staircase where consecutive cells only touched at a
// CORNER (e.g. (3,2) and (4,3)), not an edge. That was invisible before
// this feature because every old anchor shared an axis with the seed (so
// only one coordinate ever needed to move, degenerating to a straight
// line either way) -- with independent per-door offsets, anchors
// routinely differ on both axes, and the diagonal version left the
// anchor end of the bridge completely disconnected from 4-directional
// player movement despite every cell along it reading as PATH.
static void bridgeToSeed(s16 x, s16 y)
{
    s16 cx = x, cy = y;

    while (cx != ROOM_SEED_COL)
    {
        grid[cy][cx] = PATH;
        if (cx < ROOM_SEED_COL) cx++;
        else cx--;
    }
    while (cy != ROOM_SEED_ROW)
    {
        grid[cy][cx] = PATH;
        if (cy < ROOM_SEED_ROW) cy++;
        else cy--;
    }
}

void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        bool lockedN, bool lockedE, bool lockedS, bool lockedW,
                        const u8 doorOffsets[4], u8 sectionHue, u16 roomSeed)
{
    s16 x, y;
    s16 anchorX[4], anchorY[4]; // indexed by MAZE_DIR_N/E/S/W

    wallHueBase = sectionHue * WALL_VARIANTS; // spec §18: every wall cell in this room comes from that hue's block
    setRandomSeed(roomSeed);

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            grid[y][x] = randomWallVariant();

    anchorForDoor(MAZE_DIR_N, doorOffsets[MAZE_DIR_N], &anchorX[MAZE_DIR_N], &anchorY[MAZE_DIR_N]);
    anchorForDoor(MAZE_DIR_E, doorOffsets[MAZE_DIR_E], &anchorX[MAZE_DIR_E], &anchorY[MAZE_DIR_E]);
    anchorForDoor(MAZE_DIR_S, doorOffsets[MAZE_DIR_S], &anchorX[MAZE_DIR_S], &anchorY[MAZE_DIR_S]);
    anchorForDoor(MAZE_DIR_W, doorOffsets[MAZE_DIR_W], &anchorX[MAZE_DIR_W], &anchorY[MAZE_DIR_W]);

    forcedTargetCount = 0;
    if (doorN) { forcedTargetX[forcedTargetCount] = anchorX[MAZE_DIR_N]; forcedTargetY[forcedTargetCount] = anchorY[MAZE_DIR_N]; forcedTargetCount++; }
    if (doorE) { forcedTargetX[forcedTargetCount] = anchorX[MAZE_DIR_E]; forcedTargetY[forcedTargetCount] = anchorY[MAZE_DIR_E]; forcedTargetCount++; }
    if (doorS) { forcedTargetX[forcedTargetCount] = anchorX[MAZE_DIR_S]; forcedTargetY[forcedTargetCount] = anchorY[MAZE_DIR_S]; forcedTargetCount++; }
    if (doorW) { forcedTargetX[forcedTargetCount] = anchorX[MAZE_DIR_W]; forcedTargetY[forcedTargetCount] = anchorY[MAZE_DIR_W]; forcedTargetCount++; }

    carve(ROOM_SEED_COL, ROOM_SEED_ROW);

    if (doorN && (grid[anchorY[MAZE_DIR_N]][anchorX[MAZE_DIR_N]] != PATH)) bridgeToSeed(anchorX[MAZE_DIR_N], anchorY[MAZE_DIR_N]);
    if (doorE && (grid[anchorY[MAZE_DIR_E]][anchorX[MAZE_DIR_E]] != PATH)) bridgeToSeed(anchorX[MAZE_DIR_E], anchorY[MAZE_DIR_E]);
    if (doorS && (grid[anchorY[MAZE_DIR_S]][anchorX[MAZE_DIR_S]] != PATH)) bridgeToSeed(anchorX[MAZE_DIR_S], anchorY[MAZE_DIR_S]);
    if (doorW && (grid[anchorY[MAZE_DIR_W]][anchorX[MAZE_DIR_W]] != PATH)) bridgeToSeed(anchorX[MAZE_DIR_W], anchorY[MAZE_DIR_W]);

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
    // fixed-color shape. Position along the border is doorOffsets[dir]
    // now (spec §30), not always the room's own center column/row.
    if (doorN)
    {
        const s16 c = anchorX[MAZE_DIR_N];
        grid[0][c] = lockedN ? randomWallVariant() : PATH; grid[0][c + 1] = lockedN ? randomWallVariant() : PATH;
        grid[1][c] = lockedN ? randomWallVariant() : PATH; grid[1][c + 1] = lockedN ? randomWallVariant() : PATH;
    }
    if (doorS)
    {
        const s16 c = anchorX[MAZE_DIR_S];
        grid[MAZE_H - 1][c] = lockedS ? randomWallVariant() : PATH; grid[MAZE_H - 1][c + 1] = lockedS ? randomWallVariant() : PATH;
        grid[MAZE_H - 2][c] = lockedS ? randomWallVariant() : PATH; grid[MAZE_H - 2][c + 1] = lockedS ? randomWallVariant() : PATH;
    }
    if (doorE)
    {
        const s16 r = anchorY[MAZE_DIR_E];
        grid[r][MAZE_W - 1] = lockedE ? randomWallVariant() : PATH; grid[r + 1][MAZE_W - 1] = lockedE ? randomWallVariant() : PATH;
        grid[r][MAZE_W - 2] = lockedE ? randomWallVariant() : PATH; grid[r + 1][MAZE_W - 2] = lockedE ? randomWallVariant() : PATH;
    }
    if (doorW)
    {
        const s16 r = anchorY[MAZE_DIR_W];
        grid[r][0] = lockedW ? randomWallVariant() : PATH; grid[r + 1][0] = lockedW ? randomWallVariant() : PATH;
        grid[r][1] = lockedW ? randomWallVariant() : PATH; grid[r + 1][1] = lockedW ? randomWallVariant() : PATH;
    }
}

// Fixed dither cell used throughout the insertion room (spec §27) -- any
// single nonzero value works exactly as far as carve()/bridgeToSeed() are
// concerned (they only ever distinguish PATH=0 from "not yet carved"), so
// this just picks one deliberately instead of the usual randomWallVariant()
// mix, giving the room a uniform, deliberately distinct look. Never
// re-hued per section (spec §18 doesn't apply -- this room lives outside
// the grid/tree entirely, so wallHueBase is left untouched here).
#define INSERT_WALL_VARIANT 1

void Maze_generateInsertionRoom(u8 doorDir, u8 doorOffset, u16 roomSeed)
{
    s16 x, y;
    s16 anchorX, anchorY;

    setRandomSeed(roomSeed);

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            grid[y][x] = INSERT_WALL_VARIANT;

    anchorForDoor(doorDir, doorOffset, &anchorX, &anchorY);

    forcedTargetCount = 1;
    forcedTargetX[0] = anchorX;
    forcedTargetY[0] = anchorY;

    carve(ROOM_SEED_COL, ROOM_SEED_ROW);

    if (grid[anchorY][anchorX] != PATH)
        bridgeToSeed(anchorX, anchorY);

    for (x = 0; x < MAZE_W; x++)
    {
        grid[0][x] = INSERT_WALL_VARIANT;
        grid[MAZE_H - 1][x] = INSERT_WALL_VARIANT;
    }
    for (y = 0; y < MAZE_H; y++)
    {
        grid[y][0] = INSERT_WALL_VARIANT;
        grid[y][MAZE_W - 1] = INSERT_WALL_VARIANT;
    }

    // The room's single door, on whichever border/offset doorDir/
    // doorOffset picked (spec §29ter/§30: randomized once per game, no
    // longer always centered on the south border) -- its one exit "into
    // the world", leading to insertLinkCol/Row's own insertLinkDir
    // border (guidemap.c), which main.c punches open as a genuine
    // matching door on that room too (spec §29), so the player arrives
    // at (and can walk back out through) a real opening, not a blind
    // teleport into the room's center.
    switch (doorDir)
    {
        case MAZE_DIR_N:
            grid[0][anchorX] = PATH; grid[0][anchorX + 1] = PATH;
            grid[1][anchorX] = PATH; grid[1][anchorX + 1] = PATH;
            break;
        case MAZE_DIR_S:
            grid[MAZE_H - 1][anchorX] = PATH; grid[MAZE_H - 1][anchorX + 1] = PATH;
            grid[MAZE_H - 2][anchorX] = PATH; grid[MAZE_H - 2][anchorX + 1] = PATH;
            break;
        case MAZE_DIR_E:
            grid[anchorY][MAZE_W - 1] = PATH; grid[anchorY + 1][MAZE_W - 1] = PATH;
            grid[anchorY][MAZE_W - 2] = PATH; grid[anchorY + 1][MAZE_W - 2] = PATH;
            break;
        default: // MAZE_DIR_W
            grid[anchorY][0] = PATH; grid[anchorY + 1][0] = PATH;
            grid[anchorY][1] = PATH; grid[anchorY + 1][1] = PATH;
            break;
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
