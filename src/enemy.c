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

static bool wallAt(s16 px, s16 py)
{
    return Maze_isWall(toTile(px), toTile(py));
}

static bool collideUp(s16 newY, s16 x)    { return wallAt(x, newY) || wallAt(x + BOX, newY); }
static bool collideDown(s16 newY, s16 x)  { return wallAt(x, newY + BOX) || wallAt(x + BOX, newY + BOX); }
static bool collideLeft(s16 newX, s16 y)  { return wallAt(newX, y) || wallAt(newX, y + BOX); }
static bool collideRight(s16 newX, s16 y) { return wallAt(newX + BOX, y) || wallAt(newX + BOX, y + BOX); }

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

        if (!Maze_isWall(tx, ty))
        {
            *outX = tx;
            *outY = ty;
            return TRUE;
        }
    }

    return FALSE;
}

void Enemy_spawnForRoom(Enemy *e, u16 roomSeed)
{
    const u8 firstLane = (u8) (roomSeed & 3);
    s16 tx = MAZE_DOOR_COL, ty = MAZE_DOOR_ROW; // fallback: the room's own hub, always open
    bool found = FALSE;
    u8 i;

    for (i = 0; (i < 4) && !found; i++)
        found = findOpenInLane((u8) ((firstLane + i) & 3), roomSeed, &tx, &ty);

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
