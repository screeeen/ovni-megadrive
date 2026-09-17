#include "items.h"
#include "guidemap.h"
#include "maze.h"

static bool collected[ITEM_COUNT];
static u8 nextIndex; // index of the next item that CAN be collected (order enforced)

void Items_reset(void)
{
    u8 i;

    for (i = 0; i < itemCount; i++)
        collected[i] = FALSE;

    nextIndex = 0;
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

    if ((i < 0) || collected[i])
        return FALSE;

    *outLetter = (char) ('A' + i);
    return TRUE;
}

bool Items_revealedOnMap(u8 col, u8 row, char *outLetter)
{
    const s16 i = findItemAt(col, row);

    // Only the letters up to and including the one currently due (order
    // enforced, spec §13) are on the map -- collected ones stay marked as
    // a breadcrumb of the path so far, the current target is shown so the
    // player knows where to go next, but anything further ahead is not
    // revealed yet (there's no point showing where D and E are while B is
    // still due).
    if ((i < 0) || (i > (s16) nextIndex))
        return FALSE;

    *outLetter = (char) ('A' + i);
    return TRUE;
}

bool Items_isUnlocked(u8 index)
{
    return index <= nextIndex;
}

bool Items_tryCollect(u8 col, u8 row, s16 playerX, s16 playerY)
{
    s16 i;

    if (nextIndex >= itemCount)
        return FALSE; // all collected already

    i = findItemAt(col, row);
    if (i != nextIndex)
        return FALSE; // no item here, or it's not this one's turn yet

    // Same 16x16 AABB overlap as Enemy_overlaps -- the item "sprite" is a
    // single background tile, but the collision box is the same size.
    {
        const s16 itemX = MAZE_DOOR_COL * MAZE_TILE_PX;
        const s16 itemY = MAZE_DOOR_ROW * MAZE_TILE_PX;
        const bool overlap = (playerX < itemX + MAZE_TILE_PX) && (itemX < playerX + MAZE_TILE_PX) &&
                              (playerY < itemY + MAZE_TILE_PX) && (itemY < playerY + MAZE_TILE_PX);

        if (!overlap)
            return FALSE;
    }

    collected[nextIndex] = TRUE;
    nextIndex++;
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
        VDP_drawText(s, MAZE_DOOR_COL * 2, MAZE_DOOR_ROW * 2); // maze cells are 2x2 VDP tiles (maze.h)
    }
}

void Items_drawHud(void)
{
    // Sized for the largest itemCount any preset can pick (ITEM_COUNT,
    // spec §33) -- only the first itemCount slots actually get filled
    // and printed below.
    char line[2 * ITEM_COUNT];
    u8 i;

    for (i = 0; i < itemCount; i++)
    {
        line[2 * i] = collected[i] ? (char) ('A' + i) : '_';
        line[(2 * i) + 1] = ' ';
    }
    line[(2 * itemCount) - 1] = '\0'; // drop the trailing space

    VDP_setTextPriority(1); // draw above BG_A's low-priority maze tiles
    VDP_drawTextBG(BG_B, line, 1, 1);
    VDP_setTextPriority(0); // restore default for any other text draw (menu, guide map, item letters)
}
