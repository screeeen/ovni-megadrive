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

// True once every one of this game's itemCount letters has been collected
// (spec §34) -- main.c checks this when the ship walks back out through
// the insertion link to decide whether that's a normal "back to the
// insertion room, can go in again" trip or the phase is actually complete.
bool Items_allCollected(void);

// How many letters are collected right now (0..itemCount) -- same value
// as nextIndex, exposed so main.c can snapshot per-preset progress (spec
// §35) when the ship leaves a planet before finishing it.
u8 Items_collectedCount(void);

// Restores progress instantly, without touching position/overlap checks
// (spec §35): marks items 0..count-1 collected and sets nextIndex to
// count, as if they'd been picked up normally in order. Valid because
// collection is always strictly in-order (Items_tryCollect enforces it),
// so "count collected" always means exactly indices 0..count-1 -- never
// an arbitrary subset. Call right after Items_reset(), before
// GuideMap_recomputeLocks(), when resuming a planet with saved progress.
void Items_fastForward(u8 count);

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
