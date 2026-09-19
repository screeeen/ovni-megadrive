#ifndef _ITEMS_H_
#define _ITEMS_H_

#include <genesis.h>

// Clears all collected flags -- call once per newGame().
void Items_reset(void);

// True (and fills *outLetter with 'A'..'A'+itemCount-1) if room (col,row)
// holds an item that hasn't been collected yet. Used for the in-room
// letter draw -- a room can hold at most one item by construction
// (itemCol/itemRow are distinct dead-end rooms).
bool Items_uncollectedAt(u8 col, u8 row, char *outLetter);

// True (and fills *outLetter) if room (col,row) holds an item at all,
// collected or not (docs/spec-mapa-libre.md §6: any order, so there's no
// "due next" staging any more -- the guide map just shows every item's
// letter once its room has been visited, collected or still there).
bool Items_letterAt(u8 col, u8 row, char *outLetter);

// True once every one of this game's itemCount letters has been
// collected -- main.c checks this when the ship walks back out through
// the insertion link to decide whether that's a normal "back to the
// insertion room, can go in again" trip or the phase is actually
// complete.
bool Items_allCollected(void);

// How many letters are collected right now (0..itemCount) -- popcount of
// the collected-bits mask, exposed for the HUD's "X DE Y" readout.
u8 Items_collectedCount(void);

// The raw collected-bits mask (bit n set = item n collected), for
// main.c's per-preset save (docs/spec-mapa-libre.md §7: replaces the old
// "collectedCount assumes the first N in order" scheme, which no longer
// holds now that collection order is free).
u16 Items_collectedMask(void);

// Restores progress instantly from a saved mask, without touching
// position/overlap checks -- call right after Items_reset(), when
// resuming a planet with saved progress.
void Items_restoreMask(u16 mask);

// Player is in room (col,row), ship box at pixel (playerX,playerY). If
// that room holds an item that hasn't been collected yet and the ship's
// box overlaps it, marks it collected and returns TRUE (caller
// should redraw the room's letter and the HUD). Any letter can be
// collected in any order now (docs/spec-mapa-libre.md §6) -- there's no
// "next one due" restriction any more.
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
