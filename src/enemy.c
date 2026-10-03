#include "enemy.h"
#include "maze.h"
#include "player.h"

// Same probe span as player.c's own BOX -- samples both edges of the 16px
// box without landing exactly on the next cell boundary.
#define BOX (MAZE_TILE_PX - 2)

static s16 toTile(s16 px)
{
    return px >> 4;
}

// Every cell on a door's entry/exit trajectory (see markDoorTrajectory
// below), rebuilt each time Enemy_spawnForRoom runs -- one room's worth of
// state is all that's ever needed, same lifetime as the room's own grid in
// maze.c. Read back by wallAt() below, so the enemy bounces off these
// cells exactly like a real wall for the rest of its life in this room,
// not just at spawn.
static bool doorTrajectory[MAZE_H][MAZE_W];

static bool wallAt(s16 px, s16 py)
{
    return Maze_isWall(toTile(px), toTile(py));
}

// A door trajectory cell is probed with the FULL 16px box (TRAJ), not the
// 14px BOX walls use. BOX samples 2px short of the box's real edge, which
// against a wall only means the enemy ends up resting 1px inside it --
// invisible and harmless. Against a trajectory it is NOT harmless: the
// ship entering the room sits on those cells with its own full 16x16 box
// (Enemy_overlapsBox), so an enemy resting 1px into the neighbouring cell
// still overlaps it and still kills. Probing the full box makes the enemy
// stop on the cell boundary instead, leaving the trajectory completely
// untouched (user requirement: "un enemigo no puede ... colisionar con la
// trayectoria de la nave entrando en una habitacion").
#define TRAJ (MAZE_TILE_PX - 1)

static bool trajectoryAt(s16 px, s16 py)
{
    const s16 tx = toTile(px), ty = toTile(py);

    if ((tx < 0) || (ty < 0) || (tx >= MAZE_W) || (ty >= MAZE_H))
        return FALSE; // outside the room is the border wall's problem, not this one

    return doorTrajectory[ty][tx];
}

static bool collideUp(s16 newY, s16 x)
{
    return wallAt(x, newY) || wallAt(x + BOX, newY) ||
           trajectoryAt(x, newY) || trajectoryAt(x + TRAJ, newY);
}

static bool collideDown(s16 newY, s16 x)
{
    return wallAt(x, newY + BOX) || wallAt(x + BOX, newY + BOX) ||
           trajectoryAt(x, newY + TRAJ) || trajectoryAt(x + TRAJ, newY + TRAJ);
}

static bool collideLeft(s16 newX, s16 y)
{
    return wallAt(newX, y) || wallAt(newX, y + BOX) ||
           trajectoryAt(newX, y) || trajectoryAt(newX, y + TRAJ);
}

static bool collideRight(s16 newX, s16 y)
{
    return wallAt(newX + BOX, y) || wallAt(newX + BOX, y + BOX) ||
           trajectoryAt(newX + TRAJ, y) || trajectoryAt(newX + TRAJ, y + TRAJ);
}

// Scans one of the room's 4 border-adjacent lanes (lane 0/1: the row just
// inside the top/bottom wall, horizontal patrol; lane 2/3: the column just
// inside the left/right wall, vertical patrol) for an open starting cell,
// starting from a seed-derived offset along it and wrapping around the
// whole lane once. Almost always succeeds on the first cell it tries --
// obstacles are sparse (~10% of the interior, maze.c) and door lanes are
// always kept clear by construction -- but a fully-blocked lane (an
// obstacle happened to land on every single cell of it, effectively
// impossible at that density) just means this lane doesn't work, and the
// caller tries another.
static bool findOpenInLane(u8 lane, u16 seed, s16 *outX, s16 *outY)
{
    const bool horiz = (lane < 2);
    const s16 fixed = (lane == 0) ? 1 : (lane == 1) ? (MAZE_H - 2) : (lane == 2) ? 1 : (MAZE_W - 2);
    const s16 rangeMax = horiz ? (MAZE_W - 2) : (MAZE_H - 2);
    const u16 span = (u16) rangeMax; // range is [1, rangeMax], rangeMax cells
    const u16 start = seed % span;
    u16 i;

    for (i = 0; i < span; i++)
    {
        const s16 v = (s16) (1 + ((start + i) % span));
        const s16 tx = horiz ? v : fixed;
        const s16 ty = horiz ? fixed : v;

        if (!Maze_isWall(tx, ty) && !doorTrajectory[ty][tx])
        {
            *outX = tx;
            *outY = ty;
            return TRUE;
        }
    }

    return FALSE;
}

// Marks every interior cell a ship slides through right after crossing a
// door -- and, since sliding is reversible, exactly the cells it must
// cross to leave through the same door -- from the two bordering cells a
// 2-cell-wide door spans, straight inward until the first wall (spec §16's
// tomboValidate already guarantees these first steps are always open,
// never blocked by an obstacle right at the threshold). borderX/borderY is
// the door's own row/col on the border (one of the two spanning cells is
// exactly this, offset picks which -- see the two call-site coordinates
// below); dx/dy is the single inward step direction (exactly one of them
// +-1, the other 0); offset is the door's own span start (spec §30) along
// whichever axis dx/dy is 0 on.
static void markDoorTrajectory(s16 borderX, s16 borderY, s16 dx, s16 dy, u8 offset)
{
    u8 lane;

    for (lane = 0; lane < 2; lane++)
    {
        s16 x = (dx == 0) ? (s16) (offset + lane) : borderX;
        s16 y = (dy == 0) ? (s16) (offset + lane) : borderY;

        x += dx;
        y += dy;
        while (!Maze_isWall(x, y))
        {
            doorTrajectory[y][x] = TRUE; // Maze_isWall FALSE => in range
            x += dx;
            y += dy;
        }
    }
}

void Enemy_spawnForRoom(Enemy *e, u16 roomSeed, bool doorN, bool doorE, bool doorS, bool doorW,
                         const u8 doorOffsets[4])
{
    const u8 firstLane = (u8) (roomSeed & 3);
    s16 tx = MAZE_DOOR_COL, ty = MAZE_DOOR_ROW; // fallback: the room's own hub, always open
    bool found = FALSE;
    u8 i, x, y;

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            doorTrajectory[y][x] = FALSE;

    // Index convention matches maze.c's own doorOffsets[4] parameter:
    // 0=N, 1=E, 2=S, 3=W (see enemy.h's own comment).
    if (doorN) markDoorTrajectory(0, 0, 0, 1, doorOffsets[0]);
    if (doorE) markDoorTrajectory(MAZE_W - 1, 0, -1, 0, doorOffsets[1]);
    if (doorS) markDoorTrajectory(0, MAZE_H - 1, 0, -1, doorOffsets[2]);
    if (doorW) markDoorTrajectory(0, 0, 1, 0, doorOffsets[3]);

    for (i = 0; (i < 4) && !found; i++)
        found = findOpenInLane((u8) ((firstLane + i) & 3), roomSeed, &tx, &ty);

    // Last resort before giving up on a lane patrol: any interior cell at
    // all that is open and off every door trajectory. The old fallback was
    // the room's own hub, which is NOT safe here -- a door's inward walk
    // very often runs straight through it, and spawning there would park
    // the enemy right on the ship's entry path.
    if (!found)
    {
        for (y = 1; (y < MAZE_H - 1) && !found; y++)
        {
            for (x = 1; (x < MAZE_W - 1) && !found; x++)
            {
                if (!Maze_isWall(x, y) && !doorTrajectory[y][x])
                {
                    tx = x;
                    ty = y;
                    found = TRUE;
                }
            }
        }
    }

    // Nowhere legal left: this room simply gets no enemy. Better than one
    // standing on a door trajectory (same requirement as TRAJ above).
    if (!found)
    {
        e->alive = FALSE;
        e->deathTimer = 0;
        return;
    }

    e->x = (s16) (tx * MAZE_TILE_PX);
    e->y = (s16) (ty * MAZE_TILE_PX);
    // Horizontal lanes (0/1) patrol LEFT/RIGHT, vertical lanes (2/3) patrol
    // UP/DOWN -- picked straight from roomSeed bits, no call into the
    // shared random() stream (keeps this independent of whatever else
    // draws from it around the same time, same reasoning as the room's
    // own deterministic layout).
    if (firstLane < 2)
        e->dir = (roomSeed & 4) ? DIR_LEFT : DIR_RIGHT;
    else
        e->dir = (roomSeed & 4) ? DIR_UP : DIR_DOWN;

    e->state = ENEMY_DANGEROUS;
    e->stateTimer = 0;
    e->alive = TRUE;
    e->deathTimer = 0;
}

void Enemy_update(Enemy *e)
{
    if (!e->alive)
    {
        if (e->deathTimer > 0)
            e->deathTimer--;
        return;
    }

    switch (e->dir)
    {
        case DIR_UP:
        {
            const s16 newY = e->y - 1;

            if (!collideUp(newY, e->x)) e->y = newY;
            else e->dir = DIR_DOWN;
            break;
        }
        case DIR_DOWN:
        {
            const s16 newY = e->y + 1;

            if (!collideDown(newY, e->x)) e->y = newY;
            else e->dir = DIR_UP;
            break;
        }
        case DIR_LEFT:
        {
            const s16 newX = e->x - 1;

            if (!collideLeft(newX, e->y)) e->x = newX;
            else e->dir = DIR_RIGHT;
            break;
        }
        default: // DIR_RIGHT
        {
            const s16 newX = e->x + 1;

            if (!collideRight(newX, e->y)) e->x = newX;
            else e->dir = DIR_LEFT;
            break;
        }
    }

    e->stateTimer++;
    if (e->stateTimer >= ENEMY_STATE_FRAMES)
    {
        e->stateTimer = 0;
        e->state = (e->state == ENEMY_DANGEROUS) ? ENEMY_VULNERABLE : ENEMY_DANGEROUS;
    }
}

void Enemy_kill(Enemy *e)
{
    e->alive = FALSE;
    e->deathTimer = ENEMY_DEATH_BLINK_FRAMES;
}

bool Enemy_isGone(const Enemy *e)
{
    return !e->alive && (e->deathTimer == 0);
}

bool Enemy_blinkVisible(const Enemy *e)
{
    if (e->alive)
        return TRUE;
    if (e->deathTimer == 0)
        return FALSE;

    return ((e->deathTimer / ENEMY_DEATH_BLINK_PERIOD) & 1) != 0;
}

bool Enemy_overlapsBox(const Enemy *e, s16 boxX, s16 boxY)
{
    return (boxX < e->x + MAZE_TILE_PX) && (e->x < boxX + MAZE_TILE_PX) &&
           (boxY < e->y + MAZE_TILE_PX) && (e->y < boxY + MAZE_TILE_PX);
}
