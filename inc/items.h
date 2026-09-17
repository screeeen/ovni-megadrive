#ifndef _ITEMS_H_
#define _ITEMS_H_

#include <genesis.h>

// Clears all collected flags -- call once per newGame() (spec §13).
void Items_reset(void);

// True (and fills *outLetter with 'A'..'A'+itemCount-1) if room (col,row)
// holds an item that hasn't been collected yet. Used for the in-room
// letter draw -- a room can hold at most one item by construction
// (itemCol/itemRow are distinct dead-end rooms).
bool Items_uncollectedAt(u8 col, u8 row, char *outLetter);

// True (and fills *outLetter) if room (col,row) holds an item whose letter
// is due to show on the guide map: everything up to and including the one
// currently due, in order (spec §13, "solo el path disponible") --
// already-collected letters stay as a breadcrumb, the current target is
// shown, anything further ahead in the sequence is not revealed yet.
bool Items_revealedOnMap(u8 col, u8 row, char *outLetter);

// True if item `index` (0..itemCount-1) is at or before the one currently
// due -- same threshold Items_revealedOnMap uses. guidemap.c calls this to
// decide which branches of the room tree stay open vs. get sealed (spec
// §16), without needing to know about collected[]/nextIndex directly.
bool Items_isUnlocked(u8 index);

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
