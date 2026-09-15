#include "enemy.h"
#include "maze.h"
#include "player.h"

// Unlike the player, the enemy never transitions rooms -- treat the outer
// ring as always solid regardless of whether that cell is punched open as
// a door, or it can walk out through one and end up with out-of-range
// coordinates (found by fuzzing, spec §12).
static bool tileWall(s16 tx, s16 ty)
{
    if ((tx <= 0) || (ty <= 0) || (tx >= MAZE_W - 1) || (ty >= MAZE_H - 1))
        return TRUE;

    return Maze_isWall(tx, ty);
}

u8 Enemy_opposite(u8 dir)
{
    switch (dir)
    {
        case DIR_UP:    return DIR_DOWN;
        case DIR_DOWN:  return DIR_UP;
        case DIR_LEFT:  return DIR_RIGHT;
        default:        return DIR_LEFT; // DIR_RIGHT
    }
}

// Interior tiles only (spec's border-always-solid rule above already keeps
// the enemy off the outer ring anyway). Retries a bounded number of times
// against the live random() stream -- already reseeded from roomSeed by
// Maze_generateRoom just before this runs (main.c's loadRoom), so which
// tile gets picked is still deterministic per room, same persistence
// philosophy as the maze layout itself (spec §5). Falls back to the room's
// carve-seed center (always guaranteed open) if it somehow keeps rolling
// walls -- cheap insurance, not expected to actually trigger.
static bool randomOpenTile(s16 *outTx, s16 *outTy)
{
    u8 attempt;

    for (attempt = 0; attempt < 32; attempt++)
    {
        const s16 tx = 1 + (random() % (MAZE_W - 2));
        const s16 ty = 1 + (random() % (MAZE_H - 2));

        if (!Maze_isWall(tx, ty))
        {
            *outTx = tx;
            *outTy = ty;
            return TRUE;
        }
    }

    return FALSE;
}

void Enemy_spawnForRoom(Enemy *e, u16 roomSeed)
{
    s16 tx, ty;

    e->dir = roomSeed & 3; // DIR_UP..DIR_RIGHT are 0..3 (player.h) -- also fixes the enemy's axis for the room (spec §25)

    if (randomOpenTile(&tx, &ty))
    {
        e->x = tx * MAZE_TILE_PX;
        e->y = ty * MAZE_TILE_PX;
    }
    else
    {
        e->x = MAZE_DOOR_COL * MAZE_TILE_PX;
        e->y = MAZE_DOOR_ROW * MAZE_TILE_PX;
    }
}

bool Enemy_overlaps(const Enemy *a, const Enemy *b)
{
    return (a->x < b->x + MAZE_TILE_PX) && (b->x < a->x + MAZE_TILE_PX) &&
           (a->y < b->y + MAZE_TILE_PX) && (b->y < a->y + MAZE_TILE_PX);
}

// Same pixel-level box collision player.c's movePlayer() uses -- straight
// travel along a single fixed axis, reversing on collision instead of
// turning (spec §25, user request: "simple, up-down or left-right,
// bouncing off collision"). The axis is set once at spawn (Enemy_dir's
// initial UP/DOWN vs LEFT/RIGHT, spec §12) and never changes afterwards:
// DIR_UP only ever flips to DIR_DOWN and back, DIR_LEFT only to DIR_RIGHT
// and back -- there's no turning logic left to cross axes.
#define BOX (MAZE_TILE_PX - 2)

static bool wallAt(s16 px, s16 py)
{
    return tileWall(px / MAZE_TILE_PX, py / MAZE_TILE_PX);
}

static bool collideUp(s16 newY, s16 x)    { return wallAt(x, newY) || wallAt(x + BOX, newY); }
static bool collideDown(s16 newY, s16 x)  { return wallAt(x, newY + BOX) || wallAt(x + BOX, newY + BOX); }
static bool collideLeft(s16 newX, s16 y)  { return wallAt(newX, y) || wallAt(newX, y + BOX); }
static bool collideRight(s16 newX, s16 y) { return wallAt(newX + BOX, y) || wallAt(newX + BOX, y + BOX); }

void Enemy_update(Enemy *e)
{
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
}
