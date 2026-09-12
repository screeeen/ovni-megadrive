#include "player.h"
#include "maze.h"

// Same probe span as the original's `TILE - 2`: samples both edges of the
// 16px box without landing exactly on the next cell boundary.
#define BOX (MAZE_TILE_PX - 2)

static s16 toTile(s16 px)
{
    return px >> 4;
}

static bool wallAt(s16 px, s16 py)
{
    return Maze_isWall(toTile(px), toTile(py));
}

static bool collideLeft(s16 newX, s16 y)
{
    return wallAt(newX, y) || wallAt(newX, y + BOX);
}

static bool collideRight(s16 newX, s16 y)
{
    return wallAt(newX + BOX, y) || wallAt(newX + BOX, y + BOX);
}

static bool collideUp(s16 newY, s16 x)
{
    return wallAt(x, newY) || wallAt(x + BOX, newY);
}

static bool collideDown(s16 newY, s16 x)
{
    return wallAt(x, newY + BOX) || wallAt(x + BOX, newY + BOX);
}

void Player_init(Player *p)
{
    p->x = Maze_startPixelX();
    p->y = Maze_startPixelY();
    p->dir = DIR_DOWN;
}

void Player_spawnAtRoomCenter(Player *p)
{
    p->x = MAZE_DOOR_COL * MAZE_TILE_PX;
    p->y = MAZE_DOOR_ROW * MAZE_TILE_PX;
    p->dir = DIR_DOWN;
}

void Player_rotateCCW(Player *p)
{
    p->dir = (p->dir + 1) & 3;
}

void Player_rotateCW(Player *p)
{
    p->dir = (p->dir + 3) & 3;
}

static void movePlayer(Player *p)
{
    switch (p->dir)
    {
        case DIR_UP:
        {
            const s16 newY = p->y - 1;

            if (!collideUp(newY, p->x)) p->y = newY;
            else p->dir = DIR_DOWN;
            break;
        }
        case DIR_DOWN:
        {
            const s16 newY = p->y + 1;

            if (!collideDown(newY, p->x)) p->y = newY;
            else p->dir = DIR_UP;
            break;
        }
        case DIR_LEFT:
        {
            const s16 newX = p->x - 1;

            if (!collideLeft(newX, p->y)) p->x = newX;
            else p->dir = DIR_RIGHT;
            break;
        }
        case DIR_RIGHT:
        {
            const s16 newX = p->x + 1;

            if (!collideRight(newX, p->y)) p->x = newX;
            else p->dir = DIR_LEFT;
            break;
        }
    }
}

bool Player_update(Player *p)
{
    if (p->y >= MAZE_TILE_PX * (MAZE_H - 1))
        return TRUE;

    if (p->y <= 0)
    {
        p->dir = DIR_DOWN;
        p->y = 1;
    }

    movePlayer(p);

    return FALSE;
}

// tile is the door span's first column/row (MAZE_DOOR_COL or MAZE_DOOR_ROW);
// a door is 2 cells wide, so both it and the next one qualify.
static bool inDoorSpan(s16 px, s16 tile)
{
    const s16 t = toTile(px);

    return (t == tile) || (t == tile + 1);
}

u8 Player_updateRoom(Player *p, bool doorN, bool doorE, bool doorS, bool doorW)
{
    switch (p->dir)
    {
        case DIR_UP:
            if (doorN && (p->y <= 0) && inDoorSpan(p->x, MAZE_DOOR_COL))
                return EXIT_NORTH;
            break;
        case DIR_DOWN:
            if (doorS && (p->y >= MAZE_TILE_PX * (MAZE_H - 1)) && inDoorSpan(p->x, MAZE_DOOR_COL))
                return EXIT_SOUTH;
            break;
        case DIR_LEFT:
            if (doorW && (p->x <= 0) && inDoorSpan(p->y, MAZE_DOOR_ROW))
                return EXIT_WEST;
            break;
        case DIR_RIGHT:
            if (doorE && (p->x >= MAZE_TILE_PX * (MAZE_W - 1)) && inDoorSpan(p->y, MAZE_DOOR_ROW))
                return EXIT_EAST;
            break;
    }

    movePlayer(p);

    return EXIT_NONE;
}
