#include "maze.h"
#include "resources.h"

// mazeTiles.png is a 296x8 source image: 37 logical 8x8 cells in a row --
// cell 0 = floor; cells 1-36 = the dither wall variants from ovni's
// image_edit.png (matching the original's random getRandomValue() 2-10
// look), repeated once per section hue (spec §18): hue h's 9 variants
// live at cells h*9+1 .. h*9+9, same dither shapes every time, just the
// accent color swapped (violet/teal/orange/pink) -- only the background
// color (0x252525) and hue accent are used across all of them, keeping
// every hue a strict duotone. Downsampled from the original 592x16/16x16-
// per-cell art (point/nearest-neighbor, to keep every tile a strict
// duotone with no blended colors introduced) when MAZE_TILE_PX halved
// (user request: double the room's cell-grid resolution by drawing the
// tile art smaller) -- one VDP tile per logical cell now, no more 2x2
// split, so rescomp's raster slice (TILESET ... NONE NONE ROW) already
// lines up 1:1 with cell index: cell c's tile is simply BASE_TILE + c.
#define BASE_TILE TILE_USER_INDEX

#define PATH 0
#define WALL_VARIANTS 9 // 9 dither patterns per hue

static u8 grid[MAZE_H][MAZE_W];

// Offset into the current hue's 9-cell block (spec §18): 0, 9, 18 or 27,
// set once at the top of Maze_generate()/generateRoomAttempt() and read
// by every randomWallVariant() call for the rest of that room's carve.
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
// generateRoomAttempt() sets it to one entry per active door instead.
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

// The room's carve seed doubles as an item's fixed pickup position
// (items.c) -- it's the same (col,row), just named differently
// depending on which role is relevant at the call site.
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

// How far into the room from each border a door's anchor point sits --
// fixed regardless of WHERE along that border the door is (that's the
// other axis, doorOffsets[dir]/doorOffset).
#define ANCHOR_DEPTH_N 2
#define ANCHOR_DEPTH_S (MAZE_H - 4)
#define ANCHOR_DEPTH_E (MAZE_W - 4)
#define ANCHOR_DEPTH_W 2

// The actual border tile (depth 0) for a door at this offset -- a
// guaranteed chain needs to reach all the way out here, not just the
// interior anchor, since that's where a tomb-mode slide really
// starts/stops.
static void borderForDoor(u8 dir, u8 offset, s16 *outX, s16 *outY)
{
    switch (dir)
    {
        case MAZE_DIR_N: *outX = offset;       *outY = 0;              break;
        case MAZE_DIR_S: *outX = offset;       *outY = MAZE_H - 1;     break;
        case MAZE_DIR_E: *outX = MAZE_W - 1;   *outY = offset;         break;
        default:         *outX = 0;            *outY = offset;         break; // MAZE_DIR_W
    }
}

// Combines a direction's fixed depth with its along-border offset into
// the actual (x,y) anchor point carve() targets for that door.
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
// misses do happen, since door anchors can land anywhere along their
// border. Moves ONE axis at a time -- first closes the X gap (marking
// every intermediate cell along that row), then the Y gap (along the
// resulting column) -- an "L-shaped" path where every consecutive pair of
// marked cells shares a full edge (moving both axes in the same step
// would only touch at a corner, disconnected for 4-directional movement).
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

// Appends the straight-line points from the chain's current last point
// (px[*count-1],py[*count-1]) to (toX,toY). Lets the guaranteed-chain
// builders below construct ONE continuous waypoint chain across several
// straight segments (a door's own border-to-anchor stretch, the main run
// between two anchors, the far door's own anchor-to-border stretch)
// before a single carve+seal pass (carveWaypointChain) treats the WHOLE
// thing as one path -- treating each segment as its own independent
// guaranteed path leaves every anchor protected only as the ENDPOINT of
// its own segment, which a slide can sail past instead of stopping at
// (fuzz-confirmed the hard way). Making the anchor an INTERIOR point of
// one single chain -- with only the two real door borders left as true,
// unprotected ends -- closes that gap.
// yFirst picks which axis moves first when BOTH still differ (when only
// one differs, it doesn't matter, the other loop just never runs).
// Departing an anchor along the SAME axis as that door's own border sits
// on can walk straight back into that door's own 2-cell span, corrupting
// the chain with a revisited point -- departing on the PERPENDICULAR
// axis first is unconditionally safe: that axis's coordinate is fixed at
// the anchor's own value the entire time the OTHER axis still differs at
// the border, so it can never re-enter the border's own column/row range
// regardless of which way the target lies.
static void appendLine(s16 *px, s16 *py, u16 *count, s16 toX, s16 toY, bool yFirst)
{
    s16 cx = px[*count - 1], cy = py[*count - 1];

    if (yFirst)
    {
        while (cy != toY) { cy += (cy < toY) ? 1 : -1; px[*count] = cx; py[*count] = cy; (*count)++; }
        while (cx != toX) { cx += (cx < toX) ? 1 : -1; px[*count] = cx; py[*count] = cy; (*count)++; }
    }
    else
    {
        while (cx != toX) { cx += (cx < toX) ? 1 : -1; px[*count] = cx; py[*count] = cy; (*count)++; }
        while (cy != toY) { cy += (cy < toY) ? 1 : -1; px[*count] = cx; py[*count] = cy; (*count)++; }
    }
}

// Carves every point of the chain as PATH, then walls off every
// non-protected point's non-chain neighbors -- so nothing the normal
// carve()/bridgeToSeed() already put nearby can turn any point along
// this chain into a 3-or-4-way junction. That matters for tomb-mode
// sliding specifically: a ship gliding through a junction with a branch
// collinear with its direction of travel sails straight past that
// branch without ever stopping there, permanently stranding whatever is
// only reachable through it.
// useRandomVariant/fixedWallValue mirror how each kind of room fills a
// plain wall cell elsewhere: a normal room calls randomWallVariant()
// independently per cell (useRandomVariant TRUE, fixedWallValue
// ignored) to keep its usual per-cell dither variety; the uniform-look
// insertion room instead always uses the one fixed INSERT_WALL_VARIANT
// value (useRandomVariant FALSE).
// isProtected[i] marks px[i]/py[i] as an unprotected chain point (its
// neighbors are never sealed) -- a door's own border span stays
// unprotected (redundant with the border-fill loop/door-punch anyway);
// an item hub or a mid-chain waypoint must stay SEALED (protected=FALSE)
// since it needs to function as a genuine stop, not merely be passed
// through.
static void carveWaypointChain(const s16 *px, const s16 *py, const bool *isProtected, u16 count,
                                bool useRandomVariant, u8 fixedWallValue)
{
    u16 i;

    for (i = 0; i < count; i++)
        grid[py[i]][px[i]] = PATH;

    // A chain with 2+ turns can double back near itself -- a LATER point
    // can end up grid-adjacent to an EARLIER one it isn't array-adjacent
    // to (a "hook" shape). Checking only prev/next misses that: it walls
    // off a cell the chain legitimately used later, breaking the very
    // guarantee this function exists for. Checking membership in the
    // WHOLE chain instead -- not just the two array-adjacent points -- is
    // what actually needs to hold: only truly foreign cells (never part
    // of this path at all) get walled.
    for (i = 0; i < count; i++)
    {
        static const s16 nx[4] = {  0, 1, 0, -1 };
        static const s16 ny[4] = { -1, 0, 1,  0 };
        u8 d;

        if (isProtected[i])
            continue;

        for (d = 0; d < 4; d++)
        {
            const s16 tx = px[i] + nx[d];
            const s16 ty = py[i] + ny[d];
            bool inChain = FALSE;
            u16 j;

            for (j = 0; j < count; j++)
            {
                if ((px[j] == tx) && (py[j] == ty))
                {
                    inChain = TRUE;
                    break;
                }
            }

            if (!inChain && (tx > 0) && (ty > 0) && (tx < MAZE_W - 1) && (ty < MAZE_H - 1))
                grid[ty][tx] = useRandomVariant ? randomWallVariant() : fixedWallValue;
        }
    }
}

// Widens a 1-cell-wide waypoint chain into a 2-cell-wide ribbon (user
// request: "yo quiero caminos abiertos, no guiados" -- carve()'s own
// general maze is ALREADY effectively 2-cells wide throughout, since it
// moves in steps of 2 and marks 2x2 blocks per visited cell; an
// unwidened guaranteed chain stands out as a visibly narrower, obviously
// "authored" 1-cell tunnel cut through that otherwise-open space). For
// each point, adds ONE companion cell offset perpendicular to the
// chain's LOCAL direction of travel there:
//   - point 0 and the last point use the caller-supplied dx0/dy0,
//     dxLast/dyLast directly, since those are real doors, hubs, or
//     bridge attachment points with a known, deliberately chosen axis.
//   - every other (interior) point infers its own local axis from its
//     two neighbors: if they share the same X, this stretch is running
//     vertically -> widen via X; if they share the same Y, it's running
//     horizontally -> widen via Y.
//   - a TURN point (neighbors differ on BOTH axes) is deliberately left
//     alone, no companion: the only two candidate widening axes at a
//     turn are the incoming and outgoing directions themselves, and
//     offsetting a turn point along its OWN incoming direction removes
//     the very wall that makes it a stop, silently breaking the
//     guarantee (fuzz-confirmed the hard way). Every turn in the room's
//     general maze has this same single-cell pinch, so it isn't
//     visually inconsistent either.
// Companions inherit the same isProtected value as the point they widen.
// Falls back to the opposite offset, then to no companion at all, if the
// preferred direction would exit the room.
static void thickenChain(const s16 *px, const s16 *py, u16 count,
                          s16 dx0, s16 dy0, s16 dxLast, s16 dyLast,
                          s16 *outPx, s16 *outPy, bool *outProtected, u16 *outCount)
{
    u16 i;
    u16 oc = 0;

    for (i = 0; i < count; i++)
    {
        const bool protectedPoint = (i == 0) || (i == count - 1);
        s16 dx, dy;
        bool haveCompanion = TRUE;

        outPx[oc] = px[i]; outPy[oc] = py[i]; outProtected[oc] = protectedPoint; oc++;

        if (i == 0)              { dx = dx0;    dy = dy0; }
        else if (i == count - 1) { dx = dxLast; dy = dyLast; }
        else if (px[i - 1] == px[i + 1]) { dx = 1; dy = 0; } // vertical stretch
        else if (py[i - 1] == py[i + 1]) { dx = 0; dy = 1; } // horizontal stretch
        else                      { haveCompanion = FALSE; dx = 0; dy = 0; } // turn point

        if (haveCompanion)
        {
            s16 cx = px[i] + dx, cy = py[i] + dy;

            if (!((cx > 0) && (cy > 0) && (cx < MAZE_W - 1) && (cy < MAZE_H - 1)))
            {
                cx = px[i] - dx; cy = py[i] - dy; // opposite side, too close to one border
            }
            if ((cx > 0) && (cy > 0) && (cx < MAZE_W - 1) && (cy < MAZE_H - 1))
            {
                outPx[oc] = cx; outPy[oc] = cy; outProtected[oc] = protectedPoint; oc++;
            }
        }
    }

    *outCount = oc;
}

// Border-width axis for a door (spec §50): N/S doors span along X, E/W
// doors span along Y -- the natural companion offset for a door border
// point IS that door's own real 2nd span cell, already unconditionally
// reopened by the door-punch block regardless of anything this does.
static void borderWidthAxis(u8 dir, s16 *outDx, s16 *outDy)
{
    *outDx = ((dir == MAZE_DIR_N) || (dir == MAZE_DIR_S)) ? 1 : 0;
    *outDy = ((dir == MAZE_DIR_N) || (dir == MAZE_DIR_S)) ? 0 : 1;
}

// Builds and widens a plain door<->door chain (2 real door borders, no
// "stop" safety concern at either end -- crossing a border is enough,
// see Player_updateRoom's exit check, so both companions can safely use
// each door's own natural border-width axis).
static void buildDoorToDoorChain(u8 doorA, u8 doorB, const u8 doorOffsets[4],
                                  const s16 anchorX[4], const s16 anchorY[4],
                                  s16 *outPx, s16 *outPy, bool *outProtected, u16 *outCount)
{
    s16 px[4 * (MAZE_W + MAZE_H)];
    s16 py[4 * (MAZE_W + MAZE_H)];
    u16 count = 1;
    const bool midYFirst = (doorA == MAZE_DIR_E) || (doorA == MAZE_DIR_W);
    s16 dx0, dy0, dxLast, dyLast;

    borderForDoor(doorA, doorOffsets[doorA], &px[0], &py[0]);
    appendLine(px, py, &count, anchorX[doorA], anchorY[doorA], FALSE); // single-axis stretch, order irrelevant
    appendLine(px, py, &count, anchorX[doorB], anchorY[doorB], midYFirst);
    {
        s16 borderX, borderY;
        borderForDoor(doorB, doorOffsets[doorB], &borderX, &borderY);
        appendLine(px, py, &count, borderX, borderY, FALSE); // single-axis stretch, order irrelevant
    }

    borderWidthAxis(doorA, &dx0, &dy0);
    borderWidthAxis(doorB, &dxLast, &dyLast);
    thickenChain(px, py, count, dx0, dy0, dxLast, dyLast, outPx, outPy, outProtected, outCount);
}

// Builds and widens a door<->hubPoint chain: a single door's own
// border-to-anchor stretch, continuing to (hubX,hubY) as a genuine
// INTERIOR waypoint that must stay a real stop (sealed, not treated as
// an unprotected endpoint) -- used both for a dead-end room's real item
// hub (spec §49) and, doubling as a bridge attachment builder, for
// linking one group of doors to another (spec §51) when a room has 3+
// active doors. hubMustStop selects that: TRUE for a real item hub
// (Items_tryCollect only checks the RESTING position each frame, so
// sliding past it without stopping would never register); also TRUE
// for a bridge target, for the identical reason -- the whole point of a
// bridge is to actually redirect there. Widens the door border via its
// own natural axis (see buildDoorToDoorChain), and the far end
// perpendicular to however the chain actually arrives there (an
// explicit fixed axis would risk being collinear with the incoming
// direction, which would remove the very wall that makes it a stop).
static void buildDoorToPointChain(u8 door, s16 hubX, s16 hubY, const u8 doorOffsets[4],
                                   const s16 anchorX[4], const s16 anchorY[4],
                                   s16 *outPx, s16 *outPy, bool *outProtected, u16 *outCount)
{
    s16 px[4 * (MAZE_W + MAZE_H)];
    s16 py[4 * (MAZE_W + MAZE_H)];
    u16 count = 1;
    const bool leaveYFirst = (door == MAZE_DIR_E) || (door == MAZE_DIR_W);
    s16 dx0, dy0, dxLast, dyLast, inDx, inDy;

    borderForDoor(door, doorOffsets[door], &px[0], &py[0]);
    appendLine(px, py, &count, anchorX[door], anchorY[door], FALSE); // single-axis stretch, order irrelevant
    appendLine(px, py, &count, hubX, hubY, leaveYFirst);

    borderWidthAxis(door, &dx0, &dy0);
    inDx = px[count - 1] - px[count - 2];
    inDy = py[count - 1] - py[count - 2];
    dxLast = (inDy != 0) ? 1 : 0;
    dyLast = (inDx != 0) ? 1 : 0;

    thickenChain(px, py, count, dx0, dy0, dxLast, dyLast, outPx, outPy, outProtected, outCount);

    // The far end (hub or bridge target) must stay sealed -- thickenChain
    // marks both it and its own companion "protected" by default
    // (matching every other real endpoint); override that back since
    // this one specifically needs to remain a genuine stop.
    {
        const s16 lastX = px[count - 1], lastY = py[count - 1];
        u16 k;

        for (k = 0; k < *outCount; k++)
            if (((outPx[k] == lastX) && (outPy[k] == lastY)) ||
                ((outPx[k] == lastX + dxLast) && (outPy[k] == lastY + dyLast)) ||
                ((outPx[k] == lastX - dxLast) && (outPy[k] == lastY - dyLast)))
                outProtected[k] = FALSE;
    }
}

// Finds the first genuine straight interior point of an already-built
// (and possibly already-widened) chain, and its safe (in-bounds)
// perpendicular companion cell -- the bridge attachment site (spec
// §51). Returns the array index used, or count if none found
// (practically never: would need every interior point to be a turn or
// flush against a border).
static u16 findBridgeSite(const s16 *px, const s16 *py, u16 count, s16 *outBx, s16 *outBy)
{
    u16 i;

    for (i = 1; (i + 1) < count; i++)
    {
        s16 dx = 0, dy = 0;
        bool straight = FALSE;

        if (px[i - 1] == px[i + 1]) { straight = TRUE; dx = 1; dy = 0; }
        else if (py[i - 1] == py[i + 1]) { straight = TRUE; dx = 0; dy = 1; }
        if (!straight) continue;

        *outBx = px[i] + dx; *outBy = py[i] + dy;
        if (!((*outBx > 0) && (*outBy > 0) && (*outBx < MAZE_W - 1) && (*outBy < MAZE_H - 1)))
        { *outBx = px[i] - dx; *outBy = py[i] - dy; }

        if ((*outBx > 0) && (*outBy > 0) && (*outBx < MAZE_W - 1) && (*outBy < MAZE_H - 1))
            return i;
    }

    return count;
}

// Builds the guaranteed chain(s) for however many doors this room has
// active (spec §51, refactored out of the old per-visit critical-path
// design -- see docs/spec-mapa-libre.md §5): 1 door guarantees
// door<->hub (the item pickup lives there, if this room holds one); 2
// doors guarantee door<->door directly; 3-4 doors split into 2 groups
// of <=2, each with its own direct chain, connected by a single
// perpendicular bridge hop between a straight point on group1's chain
// and group2's own chain. Every sub-chain is individually widened
// (thickenChain) before combining -- concatenating already-widened
// pieces avoids the "neighboring points aren't really adjacent"
// confusion thickenChain's straight/turn inference would otherwise hit
// at the seams between pieces.
// This is deliberately NOT required to be perfect on its own: the
// caller (generateRoomAttempt via Maze_generateRoom's retry loop) only
// needs it to succeed OFTEN, and verifies + retries on failure -- see
// docs/spec-mapa-libre.md §5 for why that tradeoff is only viable now
// that generation happens once per room instead of on every real-time
// visit.
// rotation (0-3): WHICH door the N/E/S/W scan starts from when filling
// group1 first -- retrying with a different roomSeed alone does NOT
// help the dominant failure class here (fuzz-confirmed: 8/8 retries
// failed identically on a real case before this was added), because
// that class is caused by FIXED anchor/border positions coincidentally
// colliding (e.g. ANCHOR_DEPTH_W is always 2, so any OTHER door whose
// random offset happens to land near 2 creates a structural conflict
// regardless of how carve() wanders) -- doorOffsets themselves don't
// change between retries (they're shared with the neighboring room
// across that edge), so the ONLY thing that can route around a
// structural collision is trying a genuinely different CHAIN shape,
// i.e. a different group1/group2 split. Rotating the scan start
// changes exactly that.
static void buildAllDoorsChain(bool doorN, bool doorE, bool doorS, bool doorW, u8 rotation,
                                const u8 doorOffsets[4], const s16 anchorX[4], const s16 anchorY[4],
                                s16 *px, s16 *py, bool *isProtected, u16 *count)
{
    const bool active[4] = { doorN, doorE, doorS, doorW };
    u8 group1[2], group1n = 0, group2[2], group2n = 0;
    u8 i;

    for (i = 0; i < 4; i++)
    {
        const u8 d = (u8) ((rotation + i) & 3);

        if (!active[d]) continue;
        if (group1n < 2) group1[group1n++] = d; else group2[group2n++] = d;
    }

    if (group1n == 1)
    {
        // Dead end: door<->hub (spec §49) -- the item pickup, if any,
        // lives at the room's own carve seed.
        buildDoorToPointChain(group1[0], ROOM_SEED_COL, ROOM_SEED_ROW, doorOffsets, anchorX, anchorY,
                               px, py, isProtected, count);
        return;
    }

    buildDoorToDoorChain(group1[0], group1[1], doorOffsets, anchorX, anchorY, px, py, isProtected, count);

    if (group2n == 0)
        return;

    if (group2n == 1)
    {
        s16 bx, by;

        if (findBridgeSite(px, py, *count, &bx, &by) < *count)
        {
            s16 px2[2 * 4 * (MAZE_W + MAZE_H)];
            s16 py2[2 * 4 * (MAZE_W + MAZE_H)];
            bool isProtected2[2 * 4 * (MAZE_W + MAZE_H)];
            u16 count2;
            u16 k;

            buildDoorToPointChain(group2[0], bx, by, doorOffsets, anchorX, anchorY, px2, py2, isProtected2, &count2);
            for (k = 0; k < count2; k++)
            {
                px[*count] = px2[k]; py[*count] = py2[k]; isProtected[*count] = isProtected2[k];
                (*count)++;
            }
        }
        // else (no bridge site found): group2's door gets the normal,
        // not specifically guaranteed, maze layout for this attempt --
        // the retry loop's verification will simply reject this attempt
        // and try a fresh seed.
    }
    else // group2n == 2
    {
        s16 px2[4 * (MAZE_W + MAZE_H)];
        s16 py2[4 * (MAZE_W + MAZE_H)];
        bool isProtected2[4 * (MAZE_W + MAZE_H)];
        u16 count2;
        s16 bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
        u16 idx1, idx2;

        buildDoorToDoorChain(group2[0], group2[1], doorOffsets, anchorX, anchorY, px2, py2, isProtected2, &count2);

        idx1 = findBridgeSite(px, py, *count, &bx1, &by1);
        idx2 = findBridgeSite(px2, py2, count2, &bx2, &by2);

        if ((idx1 < *count) && (idx2 < count2))
        {
            // Depart bx1/by1 perpendicular to whichever axis it sits
            // offset on relative to px[idx1] (same reasoning as any
            // other anchor departure): moving along that axis first
            // cannot re-enter px[idx1]'s own straight stretch.
            const bool connectorYFirst = (bx1 != px[idx1]);
            u16 k;

            px[*count] = bx1; py[*count] = by1; isProtected[*count] = FALSE; (*count)++;
            appendLine(px, py, count, bx2, by2, connectorYFirst);
            px[*count] = px2[idx2]; py[*count] = py2[idx2]; isProtected[*count] = FALSE; (*count)++;

            for (k = 0; k < count2; k++)
            {
                px[*count] = px2[k]; py[*count] = py2[k]; isProtected[*count] = isProtected2[k];
                (*count)++;
            }
        }
        // else: see the group2n==1 comment above -- the retry loop
        // handles it.
    }
}

// ---- Generation-time BFS verification (spec §51/docs/spec-mapa-libre.md
// §5): only ever runs while building the map, once, never during real-
// time gameplay, so a room can afford up to MAZE_MAX_SEED_ATTEMPTS full
// carve+verify passes instead of needing to be perfect in one shot. ----

static bool bfsVisited[MAZE_H][MAZE_W];
static s16 bfsQueueX[MAZE_W * MAZE_H];
static s16 bfsQueueY[MAZE_W * MAZE_H];

static void slideFrom(s16 *x, s16 *y, u8 dir)
{
    s16 dx = 0, dy = 0;

    switch (dir)
    {
        case MAZE_DIR_N: dy = -1; break;
        case MAZE_DIR_S: dy = 1; break;
        case MAZE_DIR_E: dx = 1; break;
        default: dx = -1; break; // MAZE_DIR_W
    }
    while (!Maze_isWall(*x + dx, *y + dy)) { *x += dx; *y += dy; }
}

// BFS over tomb-slides from (startX,startY); returns TRUE if every one
// of targetX[0..targetCount-1]/targetY[...] is reached. Run once per
// target as the start point (reachability isn't guaranteed symmetric
// under this movement model -- a slide's stop depends on the direction
// of approach, fuzz-confirmed to actually differ by direction), so
// verifyAllReachable below calls this targetCount times.
static bool bfsReachesAll(s16 startX, s16 startY, const s16 *targetX, const s16 *targetY, u8 targetCount)
{
    u16 qHead = 0, qTail = 0;
    u8 reachedMask = 0;
    const u8 fullMask = (u8) ((1 << targetCount) - 1);
    s16 x, y;
    u8 t;

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            bfsVisited[y][x] = FALSE;

    bfsVisited[startY][startX] = TRUE;
    bfsQueueX[qTail] = startX; bfsQueueY[qTail] = startY; qTail++;

    for (t = 0; t < targetCount; t++)
        if ((startX == targetX[t]) && (startY == targetY[t]))
            reachedMask |= (u8) (1 << t);

    while ((qHead < qTail) && (reachedMask != fullMask))
    {
        const s16 cx = bfsQueueX[qHead], cy = bfsQueueY[qHead];
        u8 d;
        qHead++;

        for (d = 0; d < 4; d++)
        {
            s16 nx = cx, ny = cy;

            slideFrom(&nx, &ny, d);
            if (!bfsVisited[ny][nx])
            {
                bfsVisited[ny][nx] = TRUE;
                bfsQueueX[qTail] = nx; bfsQueueY[qTail] = ny; qTail++;
                for (t = 0; t < targetCount; t++)
                    if ((nx == targetX[t]) && (ny == targetY[t]))
                        reachedMask |= (u8) (1 << t);
            }
        }
    }

    return reachedMask == fullMask;
}

// Every one of targetCount (<=4) points mutually tomb-reachable: runs
// the BFS once from EACH point as the source, checking all the OTHERS
// are reached from it.
static bool verifyAllMutuallyReachable(const s16 *targetX, const s16 *targetY, u8 targetCount)
{
    u8 i;

    for (i = 0; i < targetCount; i++)
        if (!bfsReachesAll(targetX[i], targetY[i], targetX, targetY, targetCount))
            return FALSE;

    return TRUE;
}

// Bounded retries for the generation-time verify+retry loop (docs/
// spec-mapa-libre.md §5) -- each attempt tries roomSeed+attempt. Single-
// attempt success for the hardest (3-4 door) cases was fuzz-measured at
// 75-97% depending on which doors are involved; at 8 attempts, even the
// worst measured case's failure probability compounds to a small
// fraction of a percent, and every attempt is cheap (one 20x14 carve
// plus a handful of tiny BFS passes) -- this only ever runs while
// building the map, never during real-time play.
#define MAZE_MAX_SEED_ATTEMPTS 8

// One full carve+chain attempt for a normal room, shared by both
// Maze_generateRoom's retry loop and its own verification pass (which
// needs the SAME grid the caller will actually see).
static void generateRoomAttempt(bool doorN, bool doorE, bool doorS, bool doorW, u8 rotation,
                                 const u8 doorOffsets[4], u8 sectionHue, u16 roomSeed,
                                 s16 anchorX[4], s16 anchorY[4])
{
    s16 x, y;
    s16 px[6 * (MAZE_W + MAZE_H)];
    s16 py[6 * (MAZE_W + MAZE_H)];
    bool isProtected[6 * (MAZE_W + MAZE_H)];
    u16 count;

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

    // Guaranteed TOMB-mode-safe path(s) between however many doors this
    // room has active (docs/spec-mapa-libre.md §5), BEFORE the door
    // spans get punched open below: the chain's own sealing pass can
    // legitimately touch row/col 1 next to an anchor (only the absolute
    // border, row/col 0, is excluded), which for an N or W door is the
    // SAME row/column as that door's own second span cell -- running the
    // door punch afterwards means its unconditional per-span writes
    // always have the final say, so a span can never end up narrower
    // than its real 2 cells regardless of what this did nearby.
    buildAllDoorsChain(doorN, doorE, doorS, doorW, rotation, doorOffsets, anchorX, anchorY, px, py, isProtected, &count);
    carveWaypointChain(px, py, isProtected, count, TRUE, 0);

    // Every active door is always open now -- no lock concept (docs/
    // spec-mapa-libre.md §3): punched with independent randomWallVariant()
    // calls per cell, same as any other wall cell in this room, so it
    // blends in with the room's own hue/dither noise instead of standing
    // out as its own fixed-color shape (unused now that nothing is ever
    // sealed, but the punch itself -- unconditionally PATH -- still needs
    // to run to guarantee the span's exact width).
    if (doorN)
    {
        const s16 c = anchorX[MAZE_DIR_N];
        grid[0][c] = PATH; grid[0][c + 1] = PATH;
        grid[1][c] = PATH; grid[1][c + 1] = PATH;
    }
    if (doorS)
    {
        const s16 c = anchorX[MAZE_DIR_S];
        grid[MAZE_H - 1][c] = PATH; grid[MAZE_H - 1][c + 1] = PATH;
        grid[MAZE_H - 2][c] = PATH; grid[MAZE_H - 2][c + 1] = PATH;
    }
    if (doorE)
    {
        const s16 r = anchorY[MAZE_DIR_E];
        grid[r][MAZE_W - 1] = PATH; grid[r + 1][MAZE_W - 1] = PATH;
        grid[r][MAZE_W - 2] = PATH; grid[r + 1][MAZE_W - 2] = PATH;
    }
    if (doorW)
    {
        const s16 r = anchorY[MAZE_DIR_W];
        grid[r][0] = PATH; grid[r + 1][0] = PATH;
        grid[r][1] = PATH; grid[r + 1][1] = PATH;
    }
}

// Rotation/seed formula shared by Maze_findRoomAttempt (which searches
// for a winning attempt index, once, up front -- docs/spec-mapa-libre.md
// §5) and Maze_generateRoom (which just reproduces attempt N on demand,
// with NO retry or verification of its own, every time the room is
// actually visited during real-time play): rotation cycles every
// attempt (4 distinct group1/group2 splits for a 4-door room, fewer
// distinct outcomes for 3 or fewer, but harmless to keep cycling
// anyway); roomSeed advances every 4th attempt, so by
// MAZE_MAX_SEED_ATTEMPTS=8 both axes have been tried (all 4 rotations
// at the base carve, then all 4 again with a different carve wander) --
// see buildAllDoorsChain's own comment on why rotation is the one that
// actually matters for the dominant (structural, not carve-random)
// failure class.
static void generateRoomForAttempt(bool doorN, bool doorE, bool doorS, bool doorW,
                                    const u8 doorOffsets[4], u8 sectionHue, u16 roomSeed, u8 attempt,
                                    s16 anchorX[4], s16 anchorY[4])
{
    generateRoomAttempt(doorN, doorE, doorS, doorW, (u8) (attempt & 3),
                         doorOffsets, sectionHue, (u16) (roomSeed + (attempt >> 2)), anchorX, anchorY);
}

// Finds and returns which attempt index (0..MAZE_MAX_SEED_ATTEMPTS-1)
// verifies clean for this room -- run ONCE per room, up front, while
// building the whole map (docs/spec-mapa-libre.md §5, guidemap.c's
// GuideMap_verifyAllRooms) -- never during real-time play. The caller
// stores the result (MapCell's seedAttempt) so Maze_generateRoom can
// reproduce the exact same, already-verified layout every time the
// room is actually visited, with no retry cost paid during gameplay.
// sectionHue is irrelevant to reachability (it only picks a wall-dither
// accent color), so this always searches with hue 0 regardless of what
// the room will actually be drawn with later.
u8 Maze_findRoomAttempt(bool doorN, bool doorE, bool doorS, bool doorW,
                         const u8 doorOffsets[4], u16 roomSeed, bool hasItem)
{
    s16 anchorX[4], anchorY[4];
    u8 attempt;

    for (attempt = 0; attempt < MAZE_MAX_SEED_ATTEMPTS; attempt++)
    {
        s16 targetX[5], targetY[5];
        u8 targetCount = 0;
        u8 d;
        const bool active[4] = { doorN, doorE, doorS, doorW };

        generateRoomForAttempt(doorN, doorE, doorS, doorW, doorOffsets, 0, roomSeed, attempt, anchorX, anchorY);

        for (d = 0; d < 4; d++)
        {
            if (!active[d]) continue;
            borderForDoor(d, doorOffsets[d], &targetX[targetCount], &targetY[targetCount]);
            targetCount++;
        }
        // A dead-end room (exactly 1 active door) also needs its item
        // hub verified, if it holds one -- items only ever live in dead
        // ends (guidemap.c's selectItemRooms), so hasItem is only ever
        // TRUE alongside a single active door in practice, but this
        // doesn't assume that -- it just adds the hub as one more
        // required point whenever the caller says this room has one.
        if (hasItem)
        {
            targetX[targetCount] = ROOM_SEED_COL;
            targetY[targetCount] = ROOM_SEED_ROW;
            targetCount++;
        }

        if (verifyAllMutuallyReachable(targetX, targetY, targetCount))
            return attempt;
        // else: try the next attempt. Falling through after the last
        // one returns MAZE_MAX_SEED_ATTEMPTS-1 anyway (still fully
        // playable in every regard except the specific pair(s) that
        // didn't verify) -- measured ~4% residual on the hardest
        // (3-4 door) cases, see maze.h's own comment on this.
    }

    return (u8) (MAZE_MAX_SEED_ATTEMPTS - 1);
}

void Maze_generateRoom(bool doorN, bool doorE, bool doorS, bool doorW,
                        const u8 doorOffsets[4], u8 sectionHue, u16 roomSeed, u8 attempt)
{
    s16 anchorX[4], anchorY[4]; // unread by the caller -- generateRoomForAttempt needs the storage regardless

    generateRoomForAttempt(doorN, doorE, doorS, doorW, doorOffsets, sectionHue, roomSeed, attempt, anchorX, anchorY);
}

// Fixed dither cell used throughout the insertion room -- any single
// nonzero value works exactly as far as carve()/bridgeToSeed() are
// concerned (they only ever distinguish PATH=0 from "not yet carved"),
// so this just picks one deliberately instead of the usual
// randomWallVariant() mix, giving the room a uniform, deliberately
// distinct look. Never re-hued per section (spec §18 doesn't apply --
// this room lives outside the grid/tree entirely, so wallHueBase is left
// untouched here).
#define INSERT_WALL_VARIANT 1

// Punches a room's 2-cell-wide door open on grid border dir, at anchor
// (ax,ay) -- shared by both of the insertion room's doors so their
// carving logic can't drift apart.
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

static void generateInsertionRoomAttempt(u8 doorDir, u8 doorOffset, u8 menuDoorDir, u8 menuDoorOffset, u16 roomSeed,
                                          s16 *outAnchorX, s16 *outAnchorY, s16 *outMenuAnchorX, s16 *outMenuAnchorY)
{
    s16 x, y;
    s16 anchorX, anchorY, menuAnchorX, menuAnchorY;
    s16 px[6 * (MAZE_W + MAZE_H)];
    s16 py[6 * (MAZE_W + MAZE_H)];
    bool isProtected[6 * (MAZE_W + MAZE_H)];
    u16 count;
    s16 dx0, dy0, dxLast, dyLast;
    const bool leaveYFirst = (doorDir == MAZE_DIR_E) || (doorDir == MAZE_DIR_W);

    setRandomSeed(roomSeed);

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            grid[y][x] = INSERT_WALL_VARIANT;

    anchorForDoor(doorDir, doorOffset, &anchorX, &anchorY);
    anchorForDoor(menuDoorDir, menuDoorOffset, &menuAnchorX, &menuAnchorY);

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

    // Guaranteed TOMB-mode-safe chain between the two doors' own borders
    // -- both of the insertion room's doors always need mutual access
    // (there's no "locked" concept anywhere any more). Runs BEFORE the
    // door-punch calls below, same reasoning as generateRoomAttempt's
    // own guaranteed chain.
    borderWidthAxis(doorDir, &dx0, &dy0);
    borderWidthAxis(menuDoorDir, &dxLast, &dyLast);
    {
        s16 rawPx[4 * (MAZE_W + MAZE_H)];
        s16 rawPy[4 * (MAZE_W + MAZE_H)];
        u16 rawCount = 1;
        s16 borderX, borderY;

        borderForDoor(doorDir, doorOffset, &rawPx[0], &rawPy[0]);
        appendLine(rawPx, rawPy, &rawCount, anchorX, anchorY, FALSE);
        appendLine(rawPx, rawPy, &rawCount, menuAnchorX, menuAnchorY, leaveYFirst);
        borderForDoor(menuDoorDir, menuDoorOffset, &borderX, &borderY);
        appendLine(rawPx, rawPy, &rawCount, borderX, borderY, FALSE); // single-axis stretch, order irrelevant

        thickenChain(rawPx, rawPy, rawCount, dx0, dy0, dxLast, dyLast, px, py, isProtected, &count);
    }
    carveWaypointChain(px, py, isProtected, count, FALSE, INSERT_WALL_VARIANT);

    // The mission door leads to/from insertLinkCol/Row's own
    // insertLinkDir border (guidemap.c), which main.c punches open as a
    // genuine matching door on that room too, so the player arrives at
    // (and can walk back out through) a real opening, not a blind
    // teleport into the room's center.
    punchBorderDoor(doorDir, anchorX, anchorY);
    // The menu-exit door -- always perpendicular to doorDir, so it's
    // never the same wall and can never collide with it. Walking through
    // it always returns to the menu (main.c), unlike the mission door
    // whose outcome depends on progress.
    punchBorderDoor(menuDoorDir, menuAnchorX, menuAnchorY);

    *outAnchorX = anchorX; *outAnchorY = anchorY;
    *outMenuAnchorX = menuAnchorX; *outMenuAnchorY = menuAnchorY;
}

// Finds and returns which attempt index verifies clean for the
// insertion room -- run ONCE, up front, alongside
// GuideMap_verifyAllRooms (docs/spec-mapa-libre.md §5), never during
// real-time play. main.c stores the result and passes it back into
// Maze_generateInsertionRoom below every time that room is actually
// (re)generated (initial spawn, and any later trip back into it).
u8 Maze_findInsertionRoomAttempt(u8 doorDir, u8 doorOffset, u8 menuDoorDir, u8 menuDoorOffset, u16 roomSeed)
{
    u8 attempt;

    for (attempt = 0; attempt < MAZE_MAX_SEED_ATTEMPTS; attempt++)
    {
        s16 anchorX, anchorY, menuAnchorX, menuAnchorY;
        s16 targetX[2], targetY[2];

        generateInsertionRoomAttempt(doorDir, doorOffset, menuDoorDir, menuDoorOffset, (u16) (roomSeed + attempt),
                                      &anchorX, &anchorY, &menuAnchorX, &menuAnchorY);

        borderForDoor(doorDir, doorOffset, &targetX[0], &targetY[0]);
        borderForDoor(menuDoorDir, menuDoorOffset, &targetX[1], &targetY[1]);

        if (verifyAllMutuallyReachable(targetX, targetY, 2))
            return attempt;
    }

    return (u8) (MAZE_MAX_SEED_ATTEMPTS - 1);
}

void Maze_generateInsertionRoom(u8 doorDir, u8 doorOffset, u8 menuDoorDir, u8 menuDoorOffset, u16 roomSeed, u8 attempt)
{
    s16 anchorX, anchorY, menuAnchorX, menuAnchorY; // unread by the caller -- storage only

    generateInsertionRoomAttempt(doorDir, doorOffset, menuDoorDir, menuDoorOffset, (u16) (roomSeed + attempt),
                                  &anchorX, &anchorY, &menuAnchorX, &menuAnchorY);
}

void Maze_loadGraphics(void)
{
    // index0/1: exact colors from ovni's src/image_edit.png (dither wall
    // art) -- hue 0, unchanged, also what the guide map overlay always
    // uses. index2-4: the 3 extra section hues. Order/index assignment
    // verified against the compiled out/release/res/resources.s after
    // rebuilding.
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
            const u16 tile = BASE_TILE + c;

            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, FALSE, FALSE, tile), x, y);
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
