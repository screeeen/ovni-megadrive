#include "plants.h"
#include "maze.h"
#include "guidemap.h" // MAX_MAP_COLS/MAX_MAP_ROWS

// Also the width of collectedMask's per-room bitmask (a u8), so this
// can't go above 8 without widening that too.
#define PLANTS_MAX_PER_ROOM   5
#define PLANTS_LINE_MIN       3
// Random (anchor,axis,dir) tries before giving up on this room -- tombo
// rooms are ~90% open interior (maze.h's own doc comment), so this
// converges fast in practice, same reasoning as enemy.c's findOpenInLane.
#define PLANTS_SPAWN_ATTEMPTS 24

static u8 curCol[PLANTS_MAX_PER_ROOM];
static u8 curRow[PLANTS_MAX_PER_ROOM];
static u8 curCount;

// Persists across room re-entries (like guidemap.c's own MapCell.enemyDead),
// but -- unlike enemyDead -- needs more than 1 bit per room, so it's a
// separate array here instead of packed into MapCell. Bit i = plant i
// (this room's curCol[i]/curRow[i], deterministic from roomSeed) already
// collected.
static u8 collectedMask[MAX_MAP_ROWS][MAX_MAP_COLS];
static u16 total;

// Own tiny xorshift PRNG, seeded from roomSeed, instead of the shared
// random() stream: Maze_generateRoom's accepted-attempt cache (maze.h)
// can replay a cached layout WITHOUT repeating its original search, so
// the shared stream's state after Maze_generateRoom returns is NOT
// reliably the same on every visit to the same room -- same reasoning
// enemy.c's own doc comment gives for avoiding it.
static u32 rngState;

static void seedRng(u16 roomSeed)
{
    u32 h = (u32) roomSeed;

    h = h * 2654435761u + 0x9E3779B9u; // distinct mixing constant from main.c's roomSeedFor/enemy.c's hashing
    h ^= h >> 13;
    rngState = h ? h : 1; // xorshift needs a nonzero state
}

static u32 rngNext(void)
{
    u32 x = rngState;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;

    return x;
}

// Also excludes the room's own hub cell (the item letter's fixed spot,
// maze.h's MAZE_DOOR_COL/ROW) so a plant glyph never overlaps it -- same
// reasoning a real wall would block the line.
static bool cellBlocked(s16 x, s16 y)
{
    return Maze_isWall(x, y) || ((x == MAZE_DOOR_COL) && (y == MAZE_DOOR_ROW));
}

void Plants_reset(void)
{
    u8 r, c;

    for (r = 0; r < MAX_MAP_ROWS; r++)
        for (c = 0; c < MAX_MAP_COLS; c++)
            collectedMask[r][c] = 0;

    total = 0;
    curCount = 0;
}

void Plants_spawnForRoom(u16 roomSeed)
{
    u8 attempt;

    seedRng(roomSeed);
    curCount = 0;

    for (attempt = 0; (attempt < PLANTS_SPAWN_ATTEMPTS) && (curCount == 0); attempt++)
    {
        const bool horiz = (rngNext() & 1) != 0;
        const s16 dir = (rngNext() & 1) ? 1 : -1;
        const s16 ax = (s16) (1 + (rngNext() % (MAZE_W - 2)));
        const s16 ay = (s16) (1 + (rngNext() % (MAZE_H - 2)));
        s16 x = ax, y = ay;
        s16 cx[PLANTS_MAX_PER_ROOM], cy[PLANTS_MAX_PER_ROOM];
        u8 found = 0;

        while ((found < PLANTS_MAX_PER_ROOM) && !cellBlocked(x, y))
        {
            cx[found] = x;
            cy[found] = y;
            found++;
            if (horiz) x = (s16) (x + dir);
            else       y = (s16) (y + dir);
        }

        if (found >= PLANTS_LINE_MIN)
        {
            const u8 len = (u8) (PLANTS_LINE_MIN + (rngNext() % ((found - PLANTS_LINE_MIN) + 1)));
            u8 i;

            for (i = 0; i < len; i++)
            {
                curCol[i] = (u8) cx[i];
                curRow[i] = (u8) cy[i];
            }
            curCount = len;
        }
    }
}

bool Plants_tryCollect(u8 col, u8 row, s16 playerX, s16 playerY)
{
    bool any = FALSE;
    u8 i;

    for (i = 0; i < curCount; i++)
    {
        if (collectedMask[row][col] & (1 << i))
            continue;

        {
            const s16 px = curCol[i] * MAZE_TILE_PX;
            const s16 py = curRow[i] * MAZE_TILE_PX;
            const bool overlap = (playerX < px + MAZE_TILE_PX) && (px < playerX + MAZE_TILE_PX) &&
                                  (playerY < py + MAZE_TILE_PX) && (py < playerY + MAZE_TILE_PX);

            if (overlap)
            {
                collectedMask[row][col] |= (u8) (1 << i);
                total++;
                any = TRUE;
            }
        }
    }

    return any;
}

void Plants_drawInRoom(u8 col, u8 row)
{
    u8 i;

    for (i = 0; i < curCount; i++)
    {
        if (collectedMask[row][col] & (1 << i))
            continue;

        VDP_drawText("*", curCol[i] * 2, curRow[i] * 2); // maze cell -> 2x2 VDP tiles, same convention as items.c
    }
}

void Plants_drawHud(void)
{
    char buf[16];

    sprintf(buf, "PLANTAS:%3d", total);
    VDP_setTextPriority(1); // draw above BG_A's low-priority maze tiles, same as Items_drawHud
    VDP_drawTextBG(BG_B, buf, 14, 1);
    VDP_setTextPriority(0);
}
