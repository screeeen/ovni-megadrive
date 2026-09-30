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

// Unit cardinal steps, same 0=N/1=E/2=S/3=W convention Maze_slideEdge
// documents (doorOffsets[4]'s convention throughout this codebase).
static const s8 dirDX[4] = {  0, 1, 0, -1 };
static const s8 dirDY[4] = { -1, 0, 1,  0 };

// Generous upper bound on how many cells a single straight slide can ever
// cross in either axis (the larger of MAZE_W/MAZE_H).
#define PLANTS_MAX_EDGE_LEN 20

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
    const u16 nodeCount = Maze_slideNodeCount();
    u8 attempt;

    seedRng(roomSeed);
    curCount = 0;

    // nodeCount == 0: no slide-graph guarantee for this room (the rare
    // carve fallback -- maze.h's own doc comment says it's never actually
    // been observed to trigger -- or the insertion room, which never
    // builds one). No plants there rather than an unverifiable guess.
    if (nodeCount == 0)
        return;

    for (attempt = 0; (attempt < PLANTS_SPAWN_ATTEMPTS) && (curCount == 0); attempt++)
    {
        const u16 node = (u16) (rngNext() % nodeCount);
        const u8 dir = (u8) (rngNext() % 4);
        const s16 target = Maze_slideEdge(node, dir);
        s16 srcX, srcY, dstX, dstY;
        s16 px[PLANTS_MAX_EDGE_LEN], py[PLANTS_MAX_EDGE_LEN];
        u8 pathLen = 0;
        bool hitHub = FALSE;
        s16 x, y;

        if (target < 0)
            continue; // that slide runs into a wall right away, or straight out a door -- no edge there

        Maze_slideNodePos(node, &srcX, &srcY);
        Maze_slideNodePos((u16) target, &dstX, &dstY);

        // Walks every cell strictly between the two nodes, same straight
        // run the ship's own slide crosses making this exact move (spec
        // §48, user request: "las lineas de plantas tienen que estar en
        // una linea del grafo accesible para la nave") -- these two nodes
        // are both real, reachable stops (tomboValidate/tomboRebuildGraph
        // already guarantee that), so every cell strictly between them,
        // on the one straight line connecting them, is unavoidably
        // crossed by that slide too.
        //
        // BUG FIX (found by host fuzzing with locked doors): the slide
        // graph is always built as if every door in doorMask were
        // unlocked (maze.h's own doc comment on Maze_generateRoom's
        // lockedN/E/S/W: "the validator always treats doors as
        // unlocked; sealing only closes the border"), and that sealing
        // -- overwriting a locked door's threshold cells with a wall
        // tile -- happens AFTER the graph is built, so a node/edge can
        // sit exactly on what is, by the time this function runs, really
        // a wall. Maze_isWall() below (checked on the FINAL, already-
        // sealed grid) is the actual source of truth; the graph only
        // picks realistic CANDIDATE lines to try, same spirit as
        // enemy.c's own findOpenInLane re-checking Maze_isWall per cell
        // instead of trusting a precomputed lane blindly.
        x = (s16) (srcX + dirDX[dir]);
        y = (s16) (srcY + dirDY[dir]);
        while ((pathLen < PLANTS_MAX_EDGE_LEN) && !Maze_isWall(x, y))
        {
            px[pathLen] = x;
            py[pathLen] = y;
            if ((x == MAZE_DOOR_COL) && (y == MAZE_DOOR_ROW))
                hitHub = TRUE; // don't overlap the item letter's own fixed spot
            pathLen++;

            if ((x == dstX) && (y == dstY))
                break;

            x = (s16) (x + dirDX[dir]);
            y = (s16) (y + dirDY[dir]);
        }

        if (hitHub || (pathLen < PLANTS_LINE_MIN))
            continue;

        {
            const u8 cap = (pathLen < PLANTS_MAX_PER_ROOM) ? pathLen : PLANTS_MAX_PER_ROOM;
            const u8 len = (u8) (PLANTS_LINE_MIN + (rngNext() % ((cap - PLANTS_LINE_MIN) + 1)));
            const u8 startAt = (u8) (rngNext() % (pathLen - len + 1));
            u8 i;

            for (i = 0; i < len; i++)
            {
                curCol[i] = (u8) px[startAt + i];
                curRow[i] = (u8) py[startAt + i];
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
