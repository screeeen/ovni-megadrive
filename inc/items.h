#ifndef _ITEMS_H_
#define _ITEMS_H_

#include <genesis.h>

// Clears all collected flags -- call once per newGame() (spec §13).
void Items_reset(void);

// True (and fills *outLetter with 'A'..'A'+ITEM_COUNT-1) if room (col,row)
// holds an item that hasn't been collected yet. Used both for the in-room
// letter draw and the guide-map overlay label -- a room can hold at most
// one item by construction (itemCol/itemRow are distinct dead-end rooms).
bool Items_uncollectedAt(u8 col, u8 row, char *outLetter);

// Player is in room (col,row), ship box at pixel (playerX,playerY). If
// that room holds the NEXT item due in order and the ship's 16x16 box
// overlaps it, marks it collected and returns TRUE (caller should redraw
// the room's letter and the HUD). Touching a later letter out of order
// does nothing -- order is enforced, it just stays there, visible.
bool Items_tryCollect(u8 col, u8 row, s16 playerX, s16 playerY);

// Draws the item's letter at its fixed in-room tile position
// (MAZE_DOOR_COL/MAZE_DOOR_ROW -- the room's own carve seed, always an
// open PATH cell) if room (col,row) still holds an uncollected item.
// Call after Maze_draw() -- this only writes that one tile, it doesn't
// touch the rest of the plane.
void Items_drawInRoom(u8 col, u8 row);

// Draws the persistent collection HUD ("A B _ _ _" style) on BG_B at high
// priority, so it shows over BG_A's low-priority maze tiles without the
// two planes needing to coordinate further. Call once after Items_reset()
// and again every time Items_tryCollect() returns TRUE.
void Items_drawHud(void);

#endif
