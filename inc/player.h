#ifndef _PLAYER_H_
#define _PLAYER_H_

#include <genesis.h>

// The ship's ink color (user request: yellow, so it stands out against the
// violet walls). Overrides the violet baked into playerShip.png's palette
// -- every place that used to restore "playerShip.palette->data[1]" now
// restores this instead.
#define PLAYER_SHIP_COLOR RGB24_TO_VDPCOLOR(0xFFFF00)

#define DIR_UP      0
#define DIR_LEFT    1
#define DIR_DOWN    2
#define DIR_RIGHT   3
// No direction currently held (or held direction consumed and not yet
// replaced), so the ship just sits still.
#define DIR_NONE    4

// Player_updateRoom()'s return value: which border (if any) the player
// crossed through this frame (spec §7). EXIT_NONE means still inside the
// current room.
#define EXIT_NONE   0
#define EXIT_NORTH  1
#define EXIT_EAST   2
#define EXIT_SOUTH  3
#define EXIT_WEST   4

typedef struct
{
    s16 x;      // pixel position, top-left of the 16x16 box
    s16 y;
    u8  dir;
} Player;

// Places the player at the room's own hub/center (MAZE_DOOR_COL/ROW,
// maze.h), facing DIR_DOWN. Not currently called anywhere -- the very
// first spawn of a run used to land here (before an incoming door
// existed to align with), but that depended on the center always being
// on the insertion room's own path, which wasn't guaranteed and let the
// ship spawn embedded in a wall; main.c's newGame() now spawns at the
// mission door itself instead (positionPlayerEnteringViaDoorDir), the
// same guaranteed-open cell every later re-entry already uses. Kept
// around as a plain utility in case some other spawn context wants it.
void Player_spawnAtRoomCenter(Player *p);

// Advances the player through the CURRENT room (spec §7): doorN/E/S/W flag
// which borders are open. Reaching an inactive border just stops the ship
// in place; reaching an active one, aligned with its 2-cell span, returns
// which border was crossed instead -- the caller (main.c) is responsible
// for loading the next room and repositioning the player. doorOffsetN/S
// give the maze COLUMN of the north/south door's span, doorOffsetE/W give
// the maze ROW of the east/west door's span (spec §30: no longer always
// MAZE_DOOR_COL/MAZE_DOOR_ROW) -- ignored where the matching doorX is FALSE.
// main.c sets p->dir from the D-pad (a fresh press sets it, the ship then
// keeps sliding without holding anything; DIR_NONE means no direction, so
// don't move at all).
// speed (spec §42) is how many 1px sub-steps to take within this one
// call/frame -- NOT a bigger single jump, which would risk tunneling through
// a wall or overshooting the exact pixel a door-span/border check looks
// for. Stops early the instant any sub-step crosses a border.
u8 Player_updateRoom(Player *p, u8 speed, bool doorN, bool doorE, bool doorS, bool doorW,
                      u8 doorOffsetN, u8 doorOffsetE, u8 doorOffsetS, u8 doorOffsetW);

#endif
