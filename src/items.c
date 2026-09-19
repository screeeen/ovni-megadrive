#include "items.h"
#include "guidemap.h"
#include "maze.h"

// Bit n set = item n collected. Replaces the old collected[]/nextIndex
// pair (docs/spec-mapa-libre.md §6): collection is in ANY order now, so
// "how many collected" no longer implies "the first N" the way a simple
// count used to.
static u16 collectedMask;

void Items_reset(void)
{
    collectedMask = 0;
}

static s16 findItemAt(u8 col, u8 row)
{
    u8 i;

    for (i = 0; i < itemCount; i++)
        if ((itemCol[i] == col) && (itemRow[i] == row))
            return i;

    return -1;
}

bool Items_uncollectedAt(u8 col, u8 row, char *outLetter)
{
    const s16 i = findItemAt(col, row);

    if ((i < 0) || (collectedMask & (1 << i)))
        return FALSE;

    *outLetter = (char) ('A' + i);
    return TRUE;
}

bool Items_letterAt(u8 col, u8 row, char *outLetter)
{
    const s16 i = findItemAt(col, row);

    if (i < 0)
        return FALSE;

    *outLetter = (char) ('A' + i);
    return TRUE;
}

bool Items_allCollected(void)
{
    return Items_collectedCount() >= itemCount;
}

u8 Items_collectedCount(void)
{
    u8 count = 0;
    u8 i;

    for (i = 0; i < itemCount; i++)
        if (collectedMask & (1 << i))
            count++;

    return count;
}

u16 Items_collectedMask(void)
{
    return collectedMask;
}

void Items_restoreMask(u16 mask)
{
    collectedMask = mask;
}

bool Items_tryCollect(u8 col, u8 row, s16 playerX, s16 playerY)
{
    const s16 i = findItemAt(col, row);

    if ((i < 0) || (collectedMask & (1 << i)))
        return FALSE; // no item here, or already collected

    // Same MAZE_TILE_PX square AABB overlap as Enemy_overlaps used to be
    // -- the item "sprite" is a single background tile, but the collision
    // box is the same size.
    {
        const s16 itemX = MAZE_DOOR_COL * MAZE_TILE_PX;
        const s16 itemY = MAZE_DOOR_ROW * MAZE_TILE_PX;
        const bool overlap = (playerX < itemX + MAZE_TILE_PX) && (itemX < playerX + MAZE_TILE_PX) &&
                              (playerY < itemY + MAZE_TILE_PX) && (itemY < playerY + MAZE_TILE_PX);

        if (!overlap)
            return FALSE;
    }

    collectedMask |= (u16) (1 << i);
    return TRUE;
}

void Items_drawInRoom(u8 col, u8 row)
{
    char letter;

    if (Items_uncollectedAt(col, row, &letter))
    {
        char s[2];

        s[0] = letter;
        s[1] = '\0';
        VDP_drawText(s, MAZE_DOOR_COL, MAZE_DOOR_ROW); // maze cells are 1 VDP tile each (maze.h)
    }
}

void Items_drawHud(void)
{
    // Sized for the largest itemCount any preset can pick (ITEM_COUNT)
    // -- only the first itemCount slots actually get filled and printed
    // below.
    char line[2 * ITEM_COUNT];
    u8 i;

    for (i = 0; i < itemCount; i++)
    {
        line[2 * i] = (collectedMask & (1 << i)) ? (char) ('A' + i) : '_';
        line[(2 * i) + 1] = ' ';
    }
    line[(2 * itemCount) - 1] = '\0'; // drop the trailing space

    VDP_setTextPriority(1); // draw above BG_A's low-priority maze tiles
    VDP_drawTextBG(BG_B, line, 1, 1);
    VDP_setTextPriority(0); // restore default for any other text draw (menu, guide map, item letters)
}
