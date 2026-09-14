#include "enemy.h"
#include "maze.h"
#include "player.h"

static bool tileWall(s16 tx, s16 ty)
{
    // Unlike the player, the enemy never transitions rooms -- treat the
    // outer ring as always solid regardless of whether that cell is
    // punched open as a door, or it can walk out through one and end up
    // with out-of-range coordinates (found by fuzzing, spec §12).
    if ((tx <= 0) || (ty <= 0) || (tx >= MAZE_W - 1) || (ty >= MAZE_H - 1))
        return TRUE;

    return Maze_isWall(tx, ty);
}

static bool tileBlockedInDir(s16 tx, s16 ty, u8 dir)
{
    switch (dir)
    {
        case DIR_UP:    return tileWall(tx, ty - 1);
        case DIR_DOWN:  return tileWall(tx, ty + 1);
        case DIR_LEFT:  return tileWall(tx - 1, ty);
        default:        return tileWall(tx + 1, ty); // DIR_RIGHT
    }
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

// Turning right relative to a heading is the same rotation Player_rotateCW
// does ((dir+3)&3 == dir-1 mod4); left is Player_rotateCCW's (dir+1)&3.
// DIR_UP=0/LEFT=1/DOWN=2/RIGHT=3 (player.h) makes +1 a CCW step.
static u8 turnRight(u8 dir) { return (dir + 3) & 3; }
static u8 turnLeft(u8 dir)  { return (dir + 1) & 3; }

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

    e->dir = roomSeed & 3; // DIR_UP..DIR_RIGHT are 0..3 (player.h)

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

static void step(Enemy *e, u8 dir)
{
    switch (dir)
    {
        case DIR_UP:    e->y--; break;
        case DIR_DOWN:  e->y++; break;
        case DIR_LEFT:  e->x--; break;
        default:        e->x++; break; // DIR_RIGHT
    }
}

void Enemy_update(Enemy *e)
{
    // Only re-decide direction when centered on a tile (crossing a tile
    // boundary); between boundaries it just keeps moving straight, already
    // committed to crossing the tile it's on.
    //
    // Straight-line travel is the default; a turn is only considered when
    // straight ahead is actually blocked (right, then left, then reverse --
    // deterministic, no randomness). Strict "always prefer right" was
    // tried and reverted twice (spec §12): every room's carve seeds a
    // guaranteed fully-open 2x2 block right at the enemy's spawn point,
    // itself a closed loop, and "always prefer right" mathematically
    // cannot ever leave a closed loop it's dropped into -- fuzzing found
    // ~66-72% of rooms trapped the enemy there. Only turning when actually
    // blocked lets it travel the room broadly and still deterministically
    // route around real obstacles it runs into along the way.
    if (((e->x % MAZE_TILE_PX) == 0) && ((e->y % MAZE_TILE_PX) == 0))
    {
        const s16 tx = e->x / MAZE_TILE_PX;
        const s16 ty = e->y / MAZE_TILE_PX;

        if (tileBlockedInDir(tx, ty, e->dir))
        {
            const u8 right = turnRight(e->dir);
            const u8 left  = turnLeft(e->dir);

            if (!tileBlockedInDir(tx, ty, right))
                e->dir = right;
            else if (!tileBlockedInDir(tx, ty, left))
                e->dir = left;
            else
                e->dir = Enemy_opposite(e->dir); // walled ahead, right, and left: dead end, reverse
        }
    }

    step(e, e->dir);
}
