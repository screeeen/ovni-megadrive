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

// BUG FIX (user report: a screenshot under MAZE_ROOMGEN_TOMBO showing a
// fully sealed exit -- the arrow pointed at a door, but the border right
// there was solid wall). Root cause: ANCHOR_DEPTH_S/E sit 2 cells short
// of their own border's punch zone (S's punch inner row is MAZE_H-2, but
// ANCHOR_DEPTH_S is MAZE_H-4; E's punch inner col is MAZE_W-2, but
// ANCHOR_DEPTH_E is MAZE_W-4) while ANCHOR_DEPTH_N/W sit only 1 cell
// short of theirs (already touching -- N's punch inner row is 1,
// ANCHOR_DEPTH_N is 2). That extra 1-cell gap on S/E was never actually
// guaranteed closed by anything: carve()'s original recursive
// exploration usually paints through it anyway, as an incidental side
// effect of its own broad wandering near the edges, but "usually" isn't
// "always" -- it was a latent gap in the ORIGINAL carve()/bridgeToSeed()
// pipeline too, just masked by that incidental coverage often enough
// never to have been noticed. tombo's much more targeted, minimal-
// footprint corridors don't get that same luck, which is why it turned
// up reliably there. Fixes it for BOTH modes by guaranteeing the throat
// cell explicitly instead of hoping for it -- a no-op on N/W, which
// never had a gap to begin with.
static void connectAnchorToBorder(u8 dir, s16 ax, s16 ay)
{
    switch (dir)
    {
        case MAZE_DIR_S: grid[MAZE_H - 3][ax] = PATH; break;
        case MAZE_DIR_E: grid[ay][MAZE_W - 3] = PATH; break;
        default: break; // MAZE_DIR_N/MAZE_DIR_W: anchor already touches the punch zone
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

static void fillWallsRandom(void)
{
    s16 x, y;

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            grid[y][x] = randomWallVariant();
}

static void fillWallsFixed(u8 variant)
{
    s16 x, y;

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            grid[y][x] = variant;
}

// ---------------------------------------------------------------------
// "Tombo" room generation (Tomb of the Mask movement model, the only
// control scheme this generator targets -- CONTROL_TOMB in main.c: the
// ship slides in a straight line until a wall stops it, and can only
// change direction from a stop).
//
// Under that movement a room is really a directed graph whose NODES are
// the cells the ship can stop in (a wall right ahead of it) and whose
// EDGES are straight N/S/E/W slides between two nodes. "Every cell is
// 4-adjacent to a path cell" is NOT enough for the ship to get around:
// at a junction with 3+ open sides two of them are always opposite, and
// a ship sliding along that line never stops there, so a side branch off
// the middle of a corridor can be left but never entered (a one-way
// edge -- fuzzed on the previous generators, roughly 70-98% of 3-4 door
// rooms had an unreachable door, a letter the ship could never cross, or
// a pocket it could enter but never leave).
//
// So this generator does not carve corridors at all. The room is an OPEN
// floor (walls are only ~10% of the interior, TOMBO_WALL_PERCENT) with a
// scatter of small obstacles: every obstacle gives the sliding ship a stop
// on each of its sides, so the graph has lots of nodes and lots of routes
// between any two of them, instead of one path. A few obstacles are not
// random but placed on purpose, so the stops that matter exist:
//   - a DOOR FRAME beside each door (one wall cell next to its pocket), so
//     a ship sliding along the edge stops in the door's lane;
//   - two HUB FEEDERS, which put stops on the hub's row and column so the
//     ship crosses the letter's cell;
//   - a BLOCKER in the lane when two opposite doors line up.
// Then the room is only ACCEPTED after simulating real slides over the
// finished grid (tomboValidate below); a layout that fails is re-rolled.
// ---------------------------------------------------------------------

static MazeRoomGenMode roomGenMode = MAZE_ROOMGEN_CARVE;

void Maze_setRoomGenMode(MazeRoomGenMode mode)
{
    roomGenMode = mode;
}

// Unit cardinal steps indexed by MAZE_DIR_N/E/S/W (0/1/2/3).
static const s8 tomboDX[4] = {  0, 1, 0, -1 };
static const s8 tomboDY[4] = { -1, 0, 1,  0 };

static u8 tomboOpposite(u8 dir)
{
    return (u8) ((dir + 2) & 3);
}

// Random attempts per room, seeded from the room's own seed (still fully
// deterministic per roomSeed), then TOMBO_FALLBACK_ATTEMPTS more drawn
// from a seed that depends on NOTHING but this file -- so if every
// roomSeed-derived attempt fails, the outcome is a pure function of the
// door layout (which doors, and where), a domain small enough to have
// been verified exhaustively host-side (every door subset x every legal
// offset combination = ~25k inputs, all of them succeed).
#define TOMBO_STRICT_ATTEMPTS         16 // per puzzle tier
#define TOMBO_FALLBACK_ATTEMPTS       200
#define TOMBO_FALLBACK_SEED           0x5A17

static void tomboMark(s16 x, s16 y)
{
    grid[y][x] = PATH;
}

// Debug graph (user request: "pinta puntitos de todo el grafo de cada
// habitacion para debugear") is the validator's own slide graph of the
// accepted room -- every stop the ship can reach (see slideNodeX/Y below).
// tomboResetDebugGraph() empties it, for rooms that never ran the
// validator (carve fallback) so Maze_drawDebugGraph() draws nothing.
static void tomboResetDebugGraph(void);

// ---- slide graph (the validator) ------------------------------------

// An open room can have a stop on nearly every cell, so the graph is sized
// for one node per grid cell (it can never need more).
#define SLIDE_MAX_NODES (MAZE_W * MAZE_H)
#define SLIDE_NO_MOVE   (-1) // wall right ahead, the slide doesn't go anywhere
#define SLIDE_EXIT      (-2) // the slide runs out through a door

static s16 slideNodeX[SLIDE_MAX_NODES];
static s16 slideNodeY[SLIDE_MAX_NODES];
static s16 slideTo[SLIDE_MAX_NODES][4]; // node index, or SLIDE_NO_MOVE / SLIDE_EXIT
static bool slideHub[SLIDE_MAX_NODES]; // a slide from/through this node crosses the hub
static bool slideHubDir[SLIDE_MAX_NODES][4]; // ...and which specific slide does
static s16 slideIndex[MAZE_H][MAZE_W];
static u16 slideCount;

// BUG FIX (found while chasing the locked-door softlock report): slideHub[]
// was only ever set to TRUE by slideExpand, never back to FALSE. Reusing
// these static arrays across MULTIPLE validation passes in the same
// generation attempt (tomboValidate's own fully-open check, then one
// tomboValidateSubset call per locked-door subset -- tomboAllSubsetsSafe)
// let a stale TRUE for node index i, left over from a PREVIOUS pass' totally
// different node that happened to land on the same index, silently satisfy
// a LATER pass' "did the hub get crossed" check for a node that, this time,
// never actually touches it. Every place that starts a fresh graph (resets
// slideCount to 0) must call this, not just clear slideIndex/slideCount by
// hand -- since slideCount only ever tells you how many indices THIS pass
// used, not how many a PREVIOUS, possibly larger pass left dirty.
static void slideGraphReset(void)
{
    s16 x, y;

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            slideIndex[y][x] = -1;
    slideCount = 0;
    memset(slideHub, FALSE, sizeof(slideHub));
}

static void tomboResetDebugGraph(void)
{
    slideCount = 0;
}

// Slides from (x,y) in direction dir exactly like the ship would (cell by
// cell until the next cell is a wall); a border cell can only ever be
// open where a door was punched, and reaching one is leaving the room
// (Player_updateRoom's border check). Returns 1 with the stop cell in
// (*endX,*endY), or SLIDE_NO_MOVE, or SLIDE_EXIT. *hubHit is set if the
// hub cell was entered along the way.
static s16 slideRun(s16 x, s16 y, u8 dir, s16 hubX, s16 hubY, bool *hubHit, s16 *endX, s16 *endY)
{
    bool moved = FALSE;

    for (;;)
    {
        const s16 nx = x + tomboDX[dir], ny = y + tomboDY[dir];

        if (Maze_isWall(nx, ny))
            break;

        x = nx; y = ny;
        moved = TRUE;

        if ((x == hubX) && (y == hubY))
            *hubHit = TRUE;
        if ((x == 0) || (y == 0) || (x == MAZE_W - 1) || (y == MAZE_H - 1))
            return SLIDE_EXIT;
    }

    if (!moved)
        return SLIDE_NO_MOVE;

    *endX = x; *endY = y;
    return 1;
}

static s16 slideNode(s16 x, s16 y, s16 hubX, s16 hubY)
{
    if (slideIndex[y][x] >= 0)
        return slideIndex[y][x];
    if (slideCount >= SLIDE_MAX_NODES)
        return -1;

    slideNodeX[slideCount] = x;
    slideNodeY[slideCount] = y;
    slideHub[slideCount] = (x == hubX) && (y == hubY);
    slideIndex[y][x] = (s16) slideCount;

    return slideCount++;
}

// Expands every node discovered so far (and every one those discover in
// turn) into its 4 outgoing slides. FALSE if the graph outgrows
// SLIDE_MAX_NODES -- a room that busy is rejected rather than half-checked.
static bool slideExpand(s16 hubX, s16 hubY)
{
    u16 i;

    for (i = 0; i < slideCount; i++)
    {
        u8 dir;

        for (dir = 0; dir < 4; dir++)
        {
            s16 ex = 0, ey = 0;
            bool hubHit = FALSE;
            const s16 r = slideRun(slideNodeX[i], slideNodeY[i], dir, hubX, hubY, &hubHit, &ex, &ey);

            slideHubDir[i][dir] = hubHit;
            if (hubHit)
                slideHub[i] = TRUE;

            if (r == 1)
            {
                const s16 t = slideNode(ex, ey, hubX, hubY);

                if (t < 0)
                    return FALSE;
                slideTo[i][dir] = t;
            }
            else
            {
                slideTo[i][dir] = r;
            }
        }
    }

    return TRUE;
}

// Where the ship is standing right after crossing into this room through
// door `dir` (main.c's positionPlayerEnteringViaDoorDir: the cell just
// inside the border, aligned with the door's offset, still heading
// inward -- which, at CONTROL_TOMB's speed, slides it on to the first stop).
static void slideEntryCell(u8 dir, u8 offset, s16 *x, s16 *y)
{
    switch (dir)
    {
        case MAZE_DIR_N: *x = offset;        *y = 1;            break;
        case MAZE_DIR_E: *x = MAZE_W - 2;    *y = offset;       break;
        case MAZE_DIR_S: *x = offset;        *y = MAZE_H - 2;   break;
        default:         *x = 1;             *y = offset;       break; // MAZE_DIR_W
    }
}

// Puzzle depth (user request: the player should have to WORK OUT how to
// reach the doors and the letters). Counted in slides -- the moves the
// player actually makes -- from where the ship lands after entering:
//   - leaving through a different door takes at least TOMBO_EXIT_MIN_MOVES;
//   - the first slide that crosses the hub (picks up the letter) is at
//     least TOMBO_HUB_MIN_MOVES away, so the letter is never on the ship's
//     way in, and never one obvious slide from where it lands.
//   - and, in a room with 2+ doors, the hardest door-to-door trip is a real
//     puzzle of at least TOMBO_HARD_EXIT_MOVES.
// These are the thresholds of the current difficulty tier -- see
// generateRoomTombo: the strict tier is tried first, then progressively
// easier ones for the odd door layout where nothing strict turns up (down to
// no puzzle requirement at all, which is what guarantees a room always
// comes out).
typedef struct { s16 exitMin, hubMin, hardExit; } PuzzleTier;
static const PuzzleTier puzzleTiers[3] = { { 2, 4, 5 }, { 2, 3, 4 }, { 0, 0, 0 } };
static const PuzzleTier *puzzle = &puzzleTiers[0];
#define TOMBO_EXIT_MIN_MOVES  (puzzle->exitMin)
#define TOMBO_HUB_MIN_MOVES   (puzzle->hubMin)
#define TOMBO_HARD_EXIT_MOVES (puzzle->hardExit)

// Breadth-first slide distances from `start` (0 = start itself, -1 =
// unreachable) into dist[].
static void slideDistances(s16 start, s16 dist[SLIDE_MAX_NODES], u16 queue[SLIDE_MAX_NODES])
{
    u16 head = 0, tail = 0, i;

    for (i = 0; i < slideCount; i++)
        dist[i] = -1;

    dist[start] = 0;
    queue[tail++] = (u16) start;

    while (head < tail)
    {
        const u16 n = queue[head++];
        u8 dir;

        for (dir = 0; dir < 4; dir++)
        {
            const s16 t = slideTo[n][dir];

            if ((t >= 0) && (dist[t] < 0))
            {
                dist[t] = dist[n] + 1;
                queue[tail++] = (u16) t;
            }
        }
    }
}

// TRUE if, entering through door e, every other active door needs at least
// TOMBO_EXIT_MIN_MOVES slides and (if the room holds a letter) the hub is
// at least TOMBO_HUB_MIN_MOVES slides away.
static bool tomboPuzzleDeepEnough(u8 e, u8 doorMask, s16 entryNode, bool entryCrossesHub, bool hasLetter, s16 *hardest)
{
    static s16 dist[SLIDE_MAX_NODES];
    static u16 queue[SLIDE_MAX_NODES];
    u16 n;
    u8 f, dir;
    s16 hubBest = 0x7FF0;

    slideDistances(entryNode, dist, queue);

    for (f = 0; f < 4; f++)
    {
        s16 best = 0x7FF0;

        if ((f == e) || !(doorMask & (1 << f)))
            continue;

        for (n = 0; n < slideCount; n++)
            if ((dist[n] >= 0) && (slideTo[n][f] == SLIDE_EXIT) && ((dist[n] + 1) < best))
                best = dist[n] + 1;

        if (best < TOMBO_EXIT_MIN_MOVES)
            return FALSE;
        if (best > *hardest)
            *hardest = best;
    }

    if (hasLetter && (TOMBO_HUB_MIN_MOVES > 0))
    {
        if (entryCrossesHub)
            return FALSE;

        for (n = 0; n < slideCount; n++)
            if (dist[n] >= 0)
                for (dir = 0; dir < 4; dir++)
                    if (slideHubDir[n][dir] && ((dist[n] + 1) < hubBest))
                        hubBest = dist[n] + 1;

        if (hubBest < TOMBO_HUB_MIN_MOVES)
            return FALSE;
    }

    return TRUE;
}

// The acceptance test: simulates the ship on the finished grid (border
// door openings included, doors treated as unlocked -- a sealed door only
// closes the border, never the interior). Accepts only if, from wherever
// the ship can be after entering through ANY active door (and, for the
// insertion room, from its spawn at the hub):
//   1. every active door can be left through, from EVERY stop reachable
//      that way -- so the ship can never end up somewhere it can't get
//      out of, whichever door it wants next;
//   2. the hub is crossed at some point (the letter sits there);
//   3. entering never just shoots the ship straight out another door.
// BUG FIX (user report + host fuzz, see tomboValidateSubset's own doc
// comment): mirrors, on the ALREADY-BUILT fully-open grid, exactly the
// 2-cell-deep seal Maze_generateRoom's own locked-door punch applies (both
// the border row and the interior row/col right behind it) -- so a subset
// check sees precisely the walls the player will actually face. Door lanes
// are excluded from obstacle placement (tomboInDoorLane, 3 rows/cols deep),
// so both cells being toggled are always PATH in the base interior:
// sealing never has anything else to save/restore, opening always means
// PATH.
static void tomboSealDoor(u8 dir, u8 off, bool sealed)
{
    const u8 v = sealed ? (u8) 1 : PATH; // any nonzero value reads as wall pre-dither

    switch (dir)
    {
        case MAZE_DIR_N: grid[0][off] = v; grid[0][off + 1] = v; grid[1][off] = v; grid[1][off + 1] = v; break;
        case MAZE_DIR_S: grid[MAZE_H - 1][off] = v; grid[MAZE_H - 1][off + 1] = v; grid[MAZE_H - 2][off] = v; grid[MAZE_H - 2][off + 1] = v; break;
        case MAZE_DIR_E: grid[off][MAZE_W - 1] = v; grid[off + 1][MAZE_W - 1] = v; grid[off][MAZE_W - 2] = v; grid[off + 1][MAZE_W - 2] = v; break;
        default:         grid[off][0] = v; grid[off + 1][0] = v; grid[off][1] = v; grid[off + 1][1] = v; break; // MAZE_DIR_W
    }
}

// BUG FIX (user report: "estan fallando las habitaciones no se puede
// volver" -- confirmed by host fuzz: 8.6% of rooms with a mix of open/
// locked doors left the ship stuck, unable to reach an open door or the
// hub). Root cause: tomboValidate below only ever checked the room with
// EVERY active door open. But a door that exists in the tree can stay
// LOCKED for a long time (spec §16, branches not due yet) -- sealed 2
// cells deep by Maze_generateRoom's own punch, on top of the very same
// grid this validated with every door open. That can turn a plain
// pass-through cell into a brand new stop nobody ever simulated, and
// nothing guaranteed IT could still reach anything.
//
// This checks one additional configuration: `openSubset` (a subset of
// doorMask, at least one bit) is the set of doors NOT currently sealed. It
// requires the SAME two things tomboValidate's fully-open check requires --
// every stop reachable from any open entry can reach every OTHER open door,
// AND the hub gets crossed -- just skips the puzzle-depth minimums (those
// are a difficulty/flavor property of the FINAL, fully-open room; a
// temporarily locked sibling branch making an EARLIER visit easier isn't a
// correctness problem). The hub requirement stays because of one edge
// case: selectItemRooms() falls back to using the START room itself for
// one item when the map has too few dead ends, and the start room can have
// several independently-lockable doors of its own (spec §16) -- unlike
// every other item room, which is always a genuine 1-door dead end (no
// subset to check at all).
static bool tomboValidateSubset(u8 doorMask, u8 openSubset, const u8 doorOff[4], s16 hubX, s16 hubY)
{
    static bool canExit[4][SLIDE_MAX_NODES];
    static bool reach[SLIDE_MAX_NODES];
    static u16 stack[SLIDE_MAX_NODES];
    s16 entryNode[4];
    bool entryHub[4];
    u8 e, f;
    u16 i;
    bool ok = TRUE;

    for (e = 0; e < 4; e++)
        if (doorMask & (1 << e))
            tomboSealDoor(e, doorOff[e], !(openSubset & (1 << e)));

    slideGraphReset();

    for (e = 0; e < 4; e++)
        entryNode[e] = -1;

    for (e = 0; ok && (e < 4); e++)
    {
        s16 ix, iy, ex, ey;
        bool hubHit;
        s16 r;

        if (!(openSubset & (1 << e)))
            continue;

        slideEntryCell(e, doorOff[e], &ix, &iy);
        if (Maze_isWall(ix, iy)) { ok = FALSE; break; }

        hubHit = (ix == hubX) && (iy == hubY);
        r = slideRun(ix, iy, tomboOpposite(e), hubX, hubY, &hubHit, &ex, &ey);
        if (r == SLIDE_EXIT) { ok = FALSE; break; }
        if (r == SLIDE_NO_MOVE) { ex = ix; ey = iy; }

        entryNode[e] = slideNode(ex, ey, hubX, hubY);
        entryHub[e] = hubHit;
        if (entryNode[e] < 0) { ok = FALSE; break; }
    }

    if (ok && !slideExpand(hubX, hubY))
        ok = FALSE;

    if (ok)
    {
        for (f = 0; f < 4; f++)
        {
            bool changed = TRUE;

            if (!(openSubset & (1 << f)))
                continue;

            for (i = 0; i < slideCount; i++)
                canExit[f][i] = (slideTo[i][f] == SLIDE_EXIT);

            while (changed)
            {
                changed = FALSE;
                for (i = 0; i < slideCount; i++)
                {
                    u8 dir;

                    if (canExit[f][i])
                        continue;
                    for (dir = 0; dir < 4; dir++)
                    {
                        const s16 t = slideTo[i][dir];

                        if ((t >= 0) && canExit[f][t])
                        {
                            canExit[f][i] = TRUE;
                            changed = TRUE;
                            break;
                        }
                    }
                }
            }
        }

        for (e = 0; ok && (e < 4); e++)
        {
            u16 sp = 0;
            bool hubSeen; // per-entry: THIS entry must reach the hub, not just some other one

            if (entryNode[e] < 0)
                continue;

            for (i = 0; i < slideCount; i++)
                reach[i] = FALSE;
            reach[entryNode[e]] = TRUE;
            stack[sp++] = (u16) entryNode[e];
            hubSeen = entryHub[e];

            while (sp > 0)
            {
                const u16 n = stack[--sp];
                u8 dir;

                if (slideHub[n])
                    hubSeen = TRUE;

                for (dir = 0; dir < 4; dir++)
                {
                    const s16 t = slideTo[n][dir];

                    if ((t >= 0) && !reach[t])
                    {
                        reach[t] = TRUE;
                        stack[sp++] = (u16) t;
                    }
                }
            }

            if (!hubSeen)
                ok = FALSE;

            for (i = 0; ok && (i < slideCount); i++)
            {
                if (!reach[i])
                    continue;
                for (f = 0; f < 4; f++)
                    if ((openSubset & (1 << f)) && !canExit[f][i])
                        ok = FALSE;
            }
        }
    }

    for (e = 0; e < 4; e++)
        if (doorMask & (1 << e))
            tomboSealDoor(e, doorOff[e], FALSE);

    return ok;
}

// Every subset of doorMask that can EVER be the "currently open" set while
// this room is playable, besides the fully-open one tomboValidate already
// checks. A door only ever goes locked -> unlocked (never back), and per
// GuideMap_recomputeLocks each door locks/unlocks independently -- so over
// a room's life the open set only grows, but maze.c has no way to know the
// FUTURE order (that depends on where in the tree each branch's item sits,
// decided in guidemap.c). Rather than thread that through, this checks
// every nonempty subset there is: at most 15 for a 4-door room (worst
// case: the start room, whose doors can all lock/unlock independently --
// every other room always has its parent-facing door permanently open,
// which prunes this a lot in practice but isn't assumed here).
static bool tomboAllSubsetsSafe(u8 doorMask, const u8 doorOff[4], s16 hubX, s16 hubY)
{
    u8 subset;

    for (subset = 1; subset < doorMask; subset++)
    {
        if ((subset & doorMask) != subset)
            continue; // not a subset of the active doors
        if (!tomboValidateSubset(doorMask, subset, doorOff, hubX, hubY))
            return FALSE;
    }

    return TRUE; // subset == doorMask (every door open) is tomboValidate's own job
}

static bool tomboValidate(u8 doorMask, const u8 doorOff[4], s16 hubX, s16 hubY, bool spawnAtHub)
{
    static bool canExit[4][SLIDE_MAX_NODES];
    static bool reach[SLIDE_MAX_NODES];
    static u16 stack[SLIDE_MAX_NODES];
    s16 entryNode[5];
    bool entryHub[5];
    u8 e, f;
    u16 i;

    slideGraphReset();

    for (e = 0; e < 5; e++)
        entryNode[e] = -1;

    for (e = 0; e < 4; e++)
    {
        s16 ix, iy, ex, ey;
        bool hubHit;
        s16 r;

        if (!(doorMask & (1 << e)))
            continue;

        slideEntryCell(e, doorOff[e], &ix, &iy);
        if (Maze_isWall(ix, iy))
            return FALSE;

        hubHit = (ix == hubX) && (iy == hubY);
        r = slideRun(ix, iy, tomboOpposite(e), hubX, hubY, &hubHit, &ex, &ey);
        if (r == SLIDE_EXIT)
            return FALSE;
        if (r == SLIDE_NO_MOVE)
        {
            ex = ix; ey = iy;
        }

        entryNode[e] = slideNode(ex, ey, hubX, hubY);
        entryHub[e] = hubHit;
        if (entryNode[e] < 0)
            return FALSE;
    }

    if (spawnAtHub)
    {
        if (Maze_isWall(hubX, hubY))
            return FALSE;
        entryNode[4] = slideNode(hubX, hubY, hubX, hubY);
        entryHub[4] = TRUE;
        if (entryNode[4] < 0)
            return FALSE;
    }

    if (!slideExpand(hubX, hubY))
        return FALSE;

    // canExit[f][n]: from stop n, some sequence of slides leaves through door f.
    for (f = 0; f < 4; f++)
    {
        bool changed = TRUE;

        if (!(doorMask & (1 << f)))
            continue;

        for (i = 0; i < slideCount; i++)
            canExit[f][i] = (slideTo[i][f] == SLIDE_EXIT);

        while (changed)
        {
            changed = FALSE;
            for (i = 0; i < slideCount; i++)
            {
                u8 dir;

                if (canExit[f][i])
                    continue;
                for (dir = 0; dir < 4; dir++)
                {
                    const s16 t = slideTo[i][dir];

                    if ((t >= 0) && canExit[f][t])
                    {
                        canExit[f][i] = TRUE;
                        changed = TRUE;
                        break;
                    }
                }
            }
        }
    }

    for (e = 0; e < 5; e++)
    {
        u16 sp = 0;
        bool hubSeen;

        if (entryNode[e] < 0)
            continue;

        for (i = 0; i < slideCount; i++)
            reach[i] = FALSE;

        reach[entryNode[e]] = TRUE;
        stack[sp++] = (u16) entryNode[e];
        hubSeen = entryHub[e];

        while (sp > 0)
        {
            const u16 n = stack[--sp];
            u8 dir;

            if (slideHub[n])
                hubSeen = TRUE;

            for (dir = 0; dir < 4; dir++)
            {
                const s16 t = slideTo[n][dir];

                if ((t >= 0) && !reach[t])
                {
                    reach[t] = TRUE;
                    stack[sp++] = (u16) t;
                }
            }
        }

        if (!hubSeen)
            return FALSE;

        for (i = 0; i < slideCount; i++)
        {
            if (!reach[i])
                continue;
            for (f = 0; f < 4; f++)
                if ((doorMask & (1 << f)) && !canExit[f][i])
                    return FALSE;
        }
    }

    {
        s16 hardest = 0;
        u8 doors = 0;

        for (e = 0; e < 4; e++)
        {
            if (doorMask & (1 << e))
                doors++;
            if ((entryNode[e] >= 0) && !tomboPuzzleDeepEnough(e, doorMask, entryNode[e], entryHub[e], !spawnAtHub, &hardest))
                return FALSE;
        }

        if ((doors >= 2) && (hardest < TOMBO_HARD_EXIT_MOVES))
            return FALSE;
    }

    // Doors also need to survive every OTHER door in this room being
    // locked, one at a time or in combination -- see tomboAllSubsetsSafe's
    // own doc comment for why this can't just be inferred from the
    // fully-open case above.
    if (!tomboAllSubsetsSafe(doorMask, doorOff, hubX, hubY))
        return FALSE;

    return TRUE;
}

// Early reject (user request: cheaper first visit to a room). The two
// per-door puzzle minimums -- "every other door needs at least exitMin
// slides" and "the hub is at least hubMin slides away" -- are decided by
// slide counts that are exact as soon as the search is that deep, so most
// layouts that fail them can be dropped after a few slides instead of after
// the whole graph has been built. This is only ever a REJECT: it applies
// the very same conditions tomboPuzzleDeepEnough does (same distances, same
// thresholds), so a layout it rejects would have failed the full validator
// too, and one it lets through still goes through all of it. It cannot
// change which attempt is accepted.
#ifndef TOMBO_EARLY_REJECT
#define TOMBO_EARLY_REJECT 1
#endif

#define QUICK_MAX_NODES 48
static u16 quickStamp[MAZE_H][MAZE_W];
static u16 quickGen;

static bool tomboTooEasy(u8 doorMask, const u8 doorOff[4], s16 hubX, s16 hubY, bool spawnAtHub)
{
    const s16 exitMin = puzzle->exitMin;
    const s16 hubMin = spawnAtHub ? 0 : puzzle->hubMin; // no letter in the insertion room
    const s16 maxDepth = ((exitMin > hubMin) ? exitMin : hubMin) - 2; // deepest node whose slides can still be too short
    s16 qx[QUICK_MAX_NODES], qy[QUICK_MAX_NODES], qd[QUICK_MAX_NODES];
    u8 e;

    for (e = 0; e < 4; e++)
    {
        s16 ix, iy, ex = 0, ey = 0, r;
        bool hubHit;
        u16 head = 0, tail = 1;

        if (!(doorMask & (1 << e)))
            continue;

        slideEntryCell(e, doorOff[e], &ix, &iy);
        if (Maze_isWall(ix, iy))
            continue; // tomboValidate's own business

        hubHit = (ix == hubX) && (iy == hubY);
        r = slideRun(ix, iy, tomboOpposite(e), hubX, hubY, &hubHit, &ex, &ey);
        if (r == SLIDE_EXIT)
            return TRUE;
        if (r == SLIDE_NO_MOVE)
        {
            ex = ix; ey = iy;
        }
        if ((hubMin > 0) && hubHit)
            return TRUE; // the letter is on the way in

        if (maxDepth < 0)
            continue;

        if (++quickGen == 0)
        {
            s16 x, y;

            for (y = 0; y < MAZE_H; y++)
                for (x = 0; x < MAZE_W; x++)
                    quickStamp[y][x] = 0;
            quickGen = 1;
        }

        qx[0] = ex; qy[0] = ey; qd[0] = 0;
        quickStamp[ey][ex] = quickGen;

        while (head < tail)
        {
            const s16 nx = qx[head], ny = qy[head], nd = qd[head];
            u8 dir;

            head++;

            for (dir = 0; dir < 4; dir++)
            {
                s16 sx = 0, sy = 0;
                bool hh = FALSE;
                const s16 t = slideRun(nx, ny, dir, hubX, hubY, &hh, &sx, &sy);

                if ((hubMin > 0) && hh && ((nd + 1) < hubMin))
                    return TRUE;

                if (t == SLIDE_EXIT)
                {
                    if ((dir != e) && (doorMask & (1 << dir)) && ((nd + 1) < exitMin))
                        return TRUE;
                }
                else if ((t == 1) && (nd < maxDepth) && (tail < QUICK_MAX_NODES) && (quickStamp[sy][sx] != quickGen))
                {
                    quickStamp[sy][sx] = quickGen;
                    qx[tail] = sx; qy[tail] = sy; qd[tail] = nd + 1;
                    tail++;
                }
            }
        }
    }

    return FALSE;
}

// ---- structure (the generator) --------------------------------------

// Share of the room's INTERIOR cells (the 18x12 inside the border ring,
// which is always wall) that ends up as wall. The rest is open floor with
// a scatter of small obstacles: every obstacle gives the sliding ship new
// stops on all four sides, so instead of one corridor from A to B there
// are many routes between any two stops.
#define TOMBO_WALL_PERCENT 10
#define TOMBO_WALL_TARGET  (((MAZE_W - 2) * (MAZE_H - 2) * TOMBO_WALL_PERCENT) / 100)

// Obstacles stay off the border ring's inner row/column (no one-cell
// gutters along the edges) -- the bounds of where a wall cell may go.
#define OBST_X_MIN 2
#define OBST_X_MAX (MAZE_W - 3)
#define OBST_Y_MIN 2
#define OBST_Y_MAX (MAZE_H - 3)

// True if (x,y) is one of the cells right in front of an active door (the
// first 2 cells of its 2-wide lane, measured from the border): kept open so
// a door can never be walled off from inside.
static bool tomboInDoorLane(u8 doorMask, const u8 doorOff[4], s16 x, s16 y)
{
    if ((doorMask & (1 << MAZE_DIR_N)) && (y <= 2) && ((x == doorOff[MAZE_DIR_N]) || (x == doorOff[MAZE_DIR_N] + 1)))
        return TRUE;
    if ((doorMask & (1 << MAZE_DIR_S)) && (y >= MAZE_H - 3) && ((x == doorOff[MAZE_DIR_S]) || (x == doorOff[MAZE_DIR_S] + 1)))
        return TRUE;
    if ((doorMask & (1 << MAZE_DIR_E)) && (x >= MAZE_W - 3) && ((y == doorOff[MAZE_DIR_E]) || (y == doorOff[MAZE_DIR_E] + 1)))
        return TRUE;
    if ((doorMask & (1 << MAZE_DIR_W)) && (x <= 2) && ((y == doorOff[MAZE_DIR_W]) || (y == doorOff[MAZE_DIR_W] + 1)))
        return TRUE;

    return FALSE;
}

// Tries to drop a straight obstacle of `len` cells starting at (x,y),
// running east (horiz) or south. Refused if any cell is outside the
// obstacle bounds, on the hub (the letter), in a door lane, or if the
// shape would touch (even diagonally) any other wall -- obstacles stay
// separate islands, which is what keeps the room open.
static bool tomboPlaceObstacle(s16 x, s16 y, u8 len, bool horiz, u8 doorMask, const u8 doorOff[4], s16 hubX, s16 hubY)
{
    const s16 dx = horiz ? 1 : 0;
    const s16 dy = horiz ? 0 : 1;
    u8 i;
    s16 nx, ny;

    for (i = 0; i < len; i++)
    {
        const s16 cx = x + (i * dx);
        const s16 cy = y + (i * dy);

        if ((cx < OBST_X_MIN) || (cx > OBST_X_MAX) || (cy < OBST_Y_MIN) || (cy > OBST_Y_MAX))
            return FALSE;
        if (((cx == hubX) && (cy == hubY)) || tomboInDoorLane(doorMask, doorOff, cx, cy))
            return FALSE;
    }

    // Everything in the shape's 1-cell halo must still be floor, except
    // the shape's own cells (which are floor too, right now) -- i.e. the
    // whole halo must be PATH.
    for (ny = y - 1; ny <= y + (len * dy) + (1 - dy) ; ny++)
        for (nx = x - 1; nx <= x + (len * dx) + (1 - dx); nx++)
            if (grid[ny][nx] != PATH)
                return FALSE;

    for (i = 0; i < len; i++)
        grid[y + (i * dy)][x + (i * dx)] = 1;

    return TRUE;
}

// A door frame: one wall cell on the border ring's inner row/column, right
// beside the door's 2-wide pocket, on a randomly chosen side. A ship
// sliding along that row/column toward the door then stops IN the pocket's
// lane instead of running past it, so the door can be turned into from the
// room's outer loop -- without it, in an open room nothing would ever make
// the ship stop in front of a door.
static void tomboPlaceDoorFrames(u8 doorMask, const u8 doorOff[4])
{
    u8 dir;

    for (dir = 0; dir < 4; dir++)
    {
        const s16 lat = doorOff[dir] + ((random() & 1) ? 2 : -1);

        if (!(doorMask & (1 << dir)))
            continue;

        switch (dir)
        {
            case MAZE_DIR_N: grid[1][lat] = 1; break;
            case MAZE_DIR_S: grid[MAZE_H - 2][lat] = 1; break;
            case MAZE_DIR_E: grid[lat][MAZE_W - 2] = 1; break;
            default:         grid[lat][1] = 1; break; // MAZE_DIR_W
        }
    }
}

// Tries up to `tries` random spots for a single-cell obstacle at
// (x,y) = (fx(...),...) -- helper for the placement below: returns TRUE if
// one landed.
static bool tomboPlacePillarNear(s16 minX, s16 maxX, s16 minY, s16 maxY, u8 doorMask, const u8 doorOff[4], s16 hubX, s16 hubY)
{
    u8 t;

    for (t = 0; t < 20; t++)
    {
        const s16 x = minX + (random() % (maxX - minX + 1));
        const s16 y = minY + (random() % (maxY - minY + 1));

        if (tomboPlaceObstacle(x, y, 1, TRUE, doorMask, doorOff, hubX, hubY))
            return TRUE;
    }

    return FALSE;
}

// Two obstacles that make the hub (the letter's cell) a place the ship
// actually passes: one diagonal-adjacent to the hub's row, one to its
// column, each some cells out. A ship sliding along the obstacle's own
// column/row stops beside it, i.e. ON the hub's row/column, and its next
// slide crosses the hub. Without them nothing in an open room ever stops
// the ship on those lines.
static u16 tomboPlaceHubFeeders(u8 doorMask, const u8 doorOff[4], s16 hubX, s16 hubY)
{
    const s16 sx = (random() & 1) ? 1 : -1;
    const s16 sy = (random() & 1) ? 1 : -1;
    const s16 rowOff = 2 + (random() % 5); // 2..6 cells left/right of the hub, one row above/below
    const s16 colOff = 2 + (random() % 3); // 2..4 cells above/below the hub, one column left/right
    u16 placed = 0;

    if (tomboPlaceObstacle(hubX + (sx * rowOff), hubY + sy, 1, TRUE, doorMask, doorOff, hubX, hubY))
        placed++;
    if (tomboPlaceObstacle(hubX - sx, hubY + (sy * colOff), 1, TRUE, doorMask, doorOff, hubX, hubY))
        placed++;

    return placed;
}

// Two doors on opposite sides whose lanes overlap would shoot the ship
// straight from one into the other; an obstacle in the shared column/row,
// somewhere between them, stops that.
static u16 tomboBlockAlignedDoors(u8 doorMask, const u8 doorOff[4], s16 hubX, s16 hubY)
{
    u16 placed = 0;
    s16 d;

    if (((doorMask & (1 << MAZE_DIR_N)) && (doorMask & (1 << MAZE_DIR_S))))
    {
        const s16 n = doorOff[MAZE_DIR_N], so = doorOff[MAZE_DIR_S];

        for (d = 0; d < 2; d++)
        {
            const s16 c = d ? so : n;
            const s16 other = d ? n : so;

            if ((c == other) || (c == other + 1))
                if (tomboPlacePillarNear(c, c, 3, MAZE_H - 4, doorMask, doorOff, hubX, hubY))
                    placed++;
        }
    }
    if (((doorMask & (1 << MAZE_DIR_E)) && (doorMask & (1 << MAZE_DIR_W))))
    {
        const s16 e = doorOff[MAZE_DIR_E], w = doorOff[MAZE_DIR_W];

        for (d = 0; d < 2; d++)
        {
            const s16 r = d ? w : e;
            const s16 other = d ? e : w;

            if ((r == other) || (r == other + 1))
                if (tomboPlacePillarNear(3, MAZE_W - 4, r, r, doorMask, doorOff, hubX, hubY))
                    placed++;
        }
    }

    return placed;
}

// One generation attempt: an open floor, the door pockets, a scatter of
// obstacles up to the wall budget, then the validator's verdict.
static bool tomboTryOnce(u16 seed, s16 hubX, s16 hubY, u8 doorMask, const u8 doorOff[4],
                          bool useFixedWall, u8 fixedWallVariant, bool spawnAtHub, bool validate)
{
    s16 x, y;
    u8 dir;
    u16 placed = 0;
    u16 tries;

    setRandomSeed(seed);
    fillWallsFixed(useFixedWall ? fixedWallVariant : 1); // cheap: variants are only rolled for the accepted room
    tomboResetDebugGraph();

    for (y = 1; y < MAZE_H - 1; y++)
        for (x = 1; x < MAZE_W - 1; x++)
            tomboMark(x, y);

    // Door pockets across the border ring (same 2x2 cells the caller's
    // border punch opens -- done here too so the validator sees the real
    // opening).
    for (dir = 0; dir < 4; dir++)
    {
        if (!(doorMask & (1 << dir)))
            continue;

        switch (dir)
        {
            case MAZE_DIR_N: x = doorOff[dir]; tomboMark(x, 0); tomboMark(x + 1, 0); break;
            case MAZE_DIR_S: x = doorOff[dir]; tomboMark(x, MAZE_H - 1); tomboMark(x + 1, MAZE_H - 1); break;
            case MAZE_DIR_E: y = doorOff[dir]; tomboMark(MAZE_W - 1, y); tomboMark(MAZE_W - 1, y + 1); break;
            default:         y = doorOff[dir]; tomboMark(0, y); tomboMark(0, y + 1); break; // MAZE_DIR_W
        }
    }

    tomboPlaceDoorFrames(doorMask, doorOff);
    for (dir = 0; dir < 4; dir++)
        if (doorMask & (1 << dir))
            placed++;
    placed += tomboBlockAlignedDoors(doorMask, doorOff, hubX, hubY);
    placed += tomboPlaceHubFeeders(doorMask, doorOff, hubX, hubY);

    for (tries = 0; (placed < TOMBO_WALL_TARGET) && (tries < 400); tries++)
    {
        const u8 roll = random() % 10;
        u8 len = (roll < 8) ? 1 : 2; // mostly single pillars: each one is up to 4 new stops
        const bool horiz = (random() & 1);
        const s16 px = OBST_X_MIN + (random() % (OBST_X_MAX - OBST_X_MIN + 1));
        const s16 py = OBST_Y_MIN + (random() % (OBST_Y_MAX - OBST_Y_MIN + 1));

        if (len > (TOMBO_WALL_TARGET - placed))
            len = (u8) (TOMBO_WALL_TARGET - placed);

        if (tomboPlaceObstacle(px, py, len, horiz, doorMask, doorOff, hubX, hubY))
            placed += len;
    }

    // A replayed, previously accepted attempt (see generateRoomTombo) skips
    // the validator: it is the bulk of an attempt's cost, and the layout is
    // already known to pass. The debug graph stays empty then.
    if (!validate)
        return TRUE;

#if TOMBO_EARLY_REJECT
    if (tomboTooEasy(doorMask, doorOff, hubX, hubY, spawnAtHub))
        return FALSE;
#endif

    return tomboValidate(doorMask, doorOff, hubX, hubY, spawnAtHub);
}

// Attempt codes (user request: entering a room must not repeat the search
// every time). Every attempt is fully determined by roomSeed and its code --
// it reseeds the RNG, refills the grid, and the validator draws no random
// numbers -- so the code of the attempt that got accepted is all that is
// needed to rebuild the exact same room later, without the failed attempts
// before it. Codes run over the three phases in order:
//   0 .. STRICT-1                    strict puzzle, seed roomSeed + n
//   STRICT .. 2*STRICT-1             middle tier,   seed roomSeed + 0x4000 + n
//   2*STRICT .. +FALLBACK-1          no puzzle,     fixed seed (verified exhaustively)
#define TOMBO_CODE_COUNT (2 * TOMBO_STRICT_ATTEMPTS + TOMBO_FALLBACK_ATTEMPTS)

// Per-room cache of accepted attempt codes, indexed by the caller's slot
// (main.c: one per grid room, plus MAZE_INSERT_CACHE_SLOT). 0 = not known
// yet, CACHE_NO_TOMBO = every attempt was rejected (carve fallback), else
// code + 1. Cleared by Maze_clearRoomCache().
#define CACHE_NO_TOMBO 255
static u8 attemptCache[MAZE_ROOM_CACHE_SLOTS];

void Maze_clearRoomCache(void)
{
    u16 i;

    for (i = 0; i < MAZE_ROOM_CACHE_SLOTS; i++)
        attemptCache[i] = 0;
}

// Runs the attempt with this code. validate=FALSE replays a known-good one.
static bool tomboTryCode(u16 code, s16 hubX, s16 hubY, u8 doorMask, const u8 doorOff[4],
                          u16 roomSeed, bool useFixedWall, u8 fixedWallVariant, bool spawnAtHub, bool validate)
{
    u16 seed;

    if (code < TOMBO_STRICT_ATTEMPTS)
    {
        puzzle = &puzzleTiers[0];
        seed = roomSeed + code;
    }
    else if (code < 2 * TOMBO_STRICT_ATTEMPTS)
    {
        puzzle = &puzzleTiers[1];
        seed = roomSeed + 0x4000 + (code - TOMBO_STRICT_ATTEMPTS);
    }
    else
    {
        puzzle = &puzzleTiers[2];
        seed = TOMBO_FALLBACK_SEED + (code - (2 * TOMBO_STRICT_ATTEMPTS));
    }

    return tomboTryOnce(seed, hubX, hubY, doorMask, doorOff, useFixedWall, fixedWallVariant, spawnAtHub, validate);
}

// Full tombo pipeline for one room: attempts in code order (strict puzzle,
// then the middle tier, then the fixed-seed no-puzzle ones -- see the tier
// comments above), stopping at the first the validator accepts. FALSE only
// if every one is rejected -- the caller then falls back to carve() (never
// observed: see the exhaustive host fuzz in TOMBO_FALLBACK_ATTEMPTS's
// comment). If cacheSlot already holds an accepted code, only that attempt
// is replayed (same room, no search, no validation).
// useFixedWall/fixedWallVariant let the insertion room keep its own
// uniform look (spec §27) under tombo too. spawnAtHub additionally
// requires the insertion room's spawn point (the hub) to be a working
// starting position of its own.
static bool generateRoomTombo(s16 hubX, s16 hubY, u8 doorMask, const u8 doorOff[4],
                               u16 roomSeed, bool useFixedWall, u8 fixedWallVariant, bool spawnAtHub, u8 cacheSlot)
{
    u8 *const cache = &attemptCache[cacheSlot];
    bool ok = FALSE;
    u16 code;

    if (*cache == CACHE_NO_TOMBO)
        return FALSE;

    if (*cache != 0)
        ok = tomboTryCode((u16) (*cache - 1), hubX, hubY, doorMask, doorOff, roomSeed, useFixedWall, fixedWallVariant, spawnAtHub, FALSE);

    for (code = 0; !ok && (code < TOMBO_CODE_COUNT); code++)
    {
        ok = tomboTryCode(code, hubX, hubY, doorMask, doorOff, roomSeed, useFixedWall, fixedWallVariant, spawnAtHub, TRUE);
        if (ok)
            *cache = (u8) (code + 1);
    }

    if (!ok)
        *cache = CACHE_NO_TOMBO;

    if (ok && !useFixedWall)
    {
        // Attempts ran on a constant wall value; now that the layout is
        // final, give every wall cell its usual random dither variant.
        s16 x, y;

        for (y = 0; y < MAZE_H; y++)
            for (x = 0; x < MAZE_W; x++)
                if (grid[y][x] != PATH)
                    grid[y][x] = randomWallVariant();
    }

    return ok;
}

void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        bool lockedN, bool lockedE, bool lockedS, bool lockedW,
                        const u8 doorOffsets[4], u8 sectionHue, u16 roomSeed, u8 cacheSlot)
{
    s16 x, y;
    s16 anchorX[4], anchorY[4]; // indexed by MAZE_DIR_N/E/S/W

    wallHueBase = sectionHue * WALL_VARIANTS; // spec §18: every wall cell in this room comes from that hue's block

    anchorForDoor(MAZE_DIR_N, doorOffsets[MAZE_DIR_N], &anchorX[MAZE_DIR_N], &anchorY[MAZE_DIR_N]);
    anchorForDoor(MAZE_DIR_E, doorOffsets[MAZE_DIR_E], &anchorX[MAZE_DIR_E], &anchorY[MAZE_DIR_E]);
    anchorForDoor(MAZE_DIR_S, doorOffsets[MAZE_DIR_S], &anchorX[MAZE_DIR_S], &anchorY[MAZE_DIR_S]);
    anchorForDoor(MAZE_DIR_W, doorOffsets[MAZE_DIR_W], &anchorX[MAZE_DIR_W], &anchorY[MAZE_DIR_W]);

    // Required sockets (every ACTIVE door's anchor, regardless of locked
    // state -- a currently-locked door can still unlock on a later visit
    // without this room's interior ever being regenerated, so its anchor
    // must already be reachable either way, same as the carve/bridge
    // path below always guaranteed).

    if ((roomGenMode == MAZE_ROOMGEN_TOMBO) &&
        generateRoomTombo(ROOM_SEED_COL, ROOM_SEED_ROW,
                          (u8) ((doorN ? 1 : 0) | (doorE ? 2 : 0) | (doorS ? 4 : 0) | (doorW ? 8 : 0)),
                          doorOffsets, roomSeed, FALSE, 0, FALSE, cacheSlot))
    {
        // tombo succeeded -- grid is already fully carved, nothing more
        // to do here before the shared border/door-punch code below.
    }
    else
    {
        // MAZE_ROOMGEN_CARVE, or tombo exhausted every attempt (spec
        // §33/§34's fallback -- this pipeline has always shipped as
        // reliable on its own).
        setRandomSeed(roomSeed);
        fillWallsRandom();
        tomboResetDebugGraph(); // no explicit graph here -- Maze_drawDebugGraph() should draw nothing

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
    }

    // Guarantee every active door's anchor actually touches its own
    // border punch zone (see connectAnchorToBorder's own doc comment) --
    // regardless of algorithm or locked state, since a locked door can
    // still unlock later without this room's interior ever being
    // regenerated.
    if (doorN) connectAnchorToBorder(MAZE_DIR_N, anchorX[MAZE_DIR_N], anchorY[MAZE_DIR_N]);
    if (doorE) connectAnchorToBorder(MAZE_DIR_E, anchorX[MAZE_DIR_E], anchorY[MAZE_DIR_E]);
    if (doorS) connectAnchorToBorder(MAZE_DIR_S, anchorX[MAZE_DIR_S], anchorY[MAZE_DIR_S]);
    if (doorW) connectAnchorToBorder(MAZE_DIR_W, anchorX[MAZE_DIR_W], anchorY[MAZE_DIR_W]);

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

// Punches a room's 2-cell-wide door open on grid border dir, at anchor
// (ax,ay) -- shared by both of the insertion room's doors (spec §36) so
// their carving logic can't drift apart. Same 4-case shape
// Maze_generateInsertionRoom's single door used to open inline.
static void punchBorderDoor(u8 dir, s16 ax, s16 ay)
{
    switch (dir)
    {
        case MAZE_DIR_N:
            grid[0][ax] = PATH; grid[0][ax + 1] = PATH;
            grid[1][ax] = PATH; grid[1][ax + 1] = PATH;
            break;
        case MAZE_DIR_S:
            grid[MAZE_H - 1][ax] = PATH; grid[MAZE_H - 1][ax + 1] = PATH;
            grid[MAZE_H - 2][ax] = PATH; grid[MAZE_H - 2][ax + 1] = PATH;
            break;
        case MAZE_DIR_E:
            grid[ay][MAZE_W - 1] = PATH; grid[ay + 1][MAZE_W - 1] = PATH;
            grid[ay][MAZE_W - 2] = PATH; grid[ay + 1][MAZE_W - 2] = PATH;
            break;
        default: // MAZE_DIR_W
            grid[ay][0] = PATH; grid[ay + 1][0] = PATH;
            grid[ay][1] = PATH; grid[ay + 1][1] = PATH;
            break;
    }
}

void Maze_generateInsertionRoom(u8 doorDir, u8 doorOffset, u8 menuDoorDir, u8 menuDoorOffset, u16 roomSeed)
{
    s16 x, y;
    s16 anchorX, anchorY;
    s16 menuAnchorX, menuAnchorY;
    u8 insertOffsets[4] = { 0, 0, 0, 0 };

    insertOffsets[doorDir] = doorOffset;
    insertOffsets[menuDoorDir] = menuDoorOffset;

    anchorForDoor(doorDir, doorOffset, &anchorX, &anchorY);
    anchorForDoor(menuDoorDir, menuDoorOffset, &menuAnchorX, &menuAnchorY);

    if ((roomGenMode == MAZE_ROOMGEN_TOMBO) &&
        generateRoomTombo(ROOM_SEED_COL, ROOM_SEED_ROW, (u8) ((1 << doorDir) | (1 << menuDoorDir)), insertOffsets,
                          roomSeed, TRUE, INSERT_WALL_VARIANT, TRUE, MAZE_INSERT_CACHE_SLOT))
    {
        // tombo succeeded -- grid is already fully carved.
    }
    else
    {
        setRandomSeed(roomSeed);
        fillWallsFixed(INSERT_WALL_VARIANT);
        tomboResetDebugGraph(); // no explicit graph here -- Maze_drawDebugGraph() should draw nothing

        forcedTargetCount = 2;
        forcedTargetX[0] = anchorX;
        forcedTargetY[0] = anchorY;
        forcedTargetX[1] = menuAnchorX;
        forcedTargetY[1] = menuAnchorY;

        carve(ROOM_SEED_COL, ROOM_SEED_ROW);

        if (grid[anchorY][anchorX] != PATH)
            bridgeToSeed(anchorX, anchorY);
        if (grid[menuAnchorY][menuAnchorX] != PATH)
            bridgeToSeed(menuAnchorX, menuAnchorY);
    }

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

    // Guarantee both anchors actually touch their own border punch zone
    // (see connectAnchorToBorder's own doc comment) -- same fix as
    // Maze_generateRoom's, needed here too since this room's doorDir/
    // menuDoorDir can land on S or E exactly like any tree room's doors.
    connectAnchorToBorder(doorDir, anchorX, anchorY);
    connectAnchorToBorder(menuDoorDir, menuAnchorX, menuAnchorY);

    // The mission door, on whichever border/offset doorDir/doorOffset
    // picked (spec §29ter/§30: randomized once per game, no longer
    // always centered on the south border) -- leads to/from
    // insertLinkCol/Row's own insertLinkDir border (guidemap.c), which
    // main.c punches open as a genuine matching door on that room too
    // (spec §29), so the player arrives at (and can walk back out
    // through) a real opening, not a blind teleport into the room's
    // center.
    punchBorderDoor(doorDir, anchorX, anchorY);
    // The menu-exit door (spec §36) -- always perpendicular to doorDir,
    // so it's never the same wall and can never collide with it. Walking
    // through it always returns to the menu (main.c), unlike the mission
    // door whose outcome depends on progress.
    punchBorderDoor(menuDoorDir, menuAnchorX, menuAnchorY);
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
    // One 40x2-tile band per maze row, built in a buffer and written with a
    // single VDP_setTileMapDataRect call -- instead of 4 VDP_setTileMapXY
    // calls per cell (1120 per room), each of which recomputes the VRAM
    // address and issues its own control-port write.
    static u16 band[2 * MAZE_W * 2];
    s16 x, y;

    for (y = 0; y < MAZE_H; y++)
    {
        for (x = 0; x < MAZE_W; x++)
        {
            const u16 tl = BASE_TILE + (2 * grid[y][x]);
            const u16 bl = tl + CELL_ROW_TILES;
            const u16 i = (u16) (x * 2);

            band[i] = TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, tl);
            band[i + 1] = TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, tl + 1);
            band[(MAZE_W * 2) + i] = TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, bl);
            band[(MAZE_W * 2) + i + 1] = TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, bl + 1);
        }

        VDP_setTileMapDataRect(BG_A, band, 0, y * 2, MAZE_W * 2, 2, MAZE_W * 2, CPU);
    }
}

// Debug overlay (user request: "pinta puntitos de todo el grafo de cada
// habitacion para debugear") -- one small dot per node of the accepted
// room's slide graph (every cell the ship can stop in). Reuses
// VDP_drawText like every other in-room label. Draws nothing when the room
// came from MAZE_ROOMGEN_CARVE or from tombo's carve fallback
// (slideCount is reset to 0 there). Call right after Maze_draw().
void Maze_drawDebugGraph(void)
{
    u16 i;

    for (i = 0; i < slideCount; i++)
        VDP_drawText(".", (u16) (slideNodeX[i] * 2), (u16) (slideNodeY[i] * 2));
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
