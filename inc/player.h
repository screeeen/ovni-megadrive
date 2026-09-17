#ifndef _PLAYER_H_
#define _PLAYER_H_

#include <genesis.h>

#define DIR_UP      0
#define DIR_LEFT    1
#define DIR_DOWN    2
#define DIR_RIGHT   3
// "Normal" control mode only (spec §40): no direction currently held, so
// the ship just sits still. Never used in "borracho" mode -- that mode
// always has a real direction, only ever changed by rotating.
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

void Player_init(Player *p);

// Multi-room system's spawn point (spec §5): the room's carve seed, always
// a PATH cell, used only for the very first room of a run (no incoming
// door to align with).
void Player_spawnAtRoomCenter(Player *p);

// Same "rotate 90 degrees" action bound to SPACE in the original js13k game
// (counter-clockwise: UP -> LEFT -> DOWN -> RIGHT -> UP).
void Player_rotateCCW(Player *p);

// Opposite turn, not present in the original: clockwise (UP -> RIGHT -> DOWN -> LEFT -> UP).
void Player_rotateCW(Player *p);

// Advances the player one step and resolves collisions against the maze.
// Returns TRUE when the player reaches the bottom of the maze (win condition).
// Single-room prototype only (main.c's original game loop) -- untouched by
// the multi-room guide map system below.
bool Player_update(Player *p);

// Multi-room version of Player_update (spec §7): doorN/E/S/W flag which
// borders are open in the CURRENT room. Reaching an inactive border still
// bounces (drunkMode) or just stops (!drunkMode) exactly like plain
// movement would; reaching an active one, aligned with its 2-cell span,
// returns which border was crossed instead -- the caller (main.c) is
// responsible for loading the next room and repositioning the player.
// doorOffsetN/S give the maze COLUMN of the north/south door's span,
// doorOffsetE/W give the maze ROW of the east/west door's span (spec
// §30: no longer always MAZE_DOOR_COL/MAZE_DOOR_ROW) -- ignored where
// the matching doorX is FALSE.
// drunkMode (spec §40, user request): TRUE is the original js13k-style
// control this game always had -- the ship moves on its own every frame
// in whatever direction p->dir currently is, turning 90 degrees only via
// Player_rotateCW/CCW, and bounces (reverses direction) off a wall
// instead of stopping. FALSE is the "normal" mode -- main.c sets p->dir
// directly from the D-pad (spec §41: a fresh press sets it, it then
// keeps going at a constant rate without needing to hold anything;
// DIR_NONE means no direction has been pressed yet, so don't move at
// all), and bumping into a wall just stops the ship in place instead of
// reversing.
// speed (spec §42, user request: "que vaya más rápido en el modo
// normal") is how many 1px sub-steps to take within this one call/frame
// -- NOT a bigger single jump, which would risk tunneling through a
// wall or overshooting the exact pixel a door-span/border check looks
// for. 1 reproduces the original per-frame behavior exactly. Stops
// early the instant any sub-step crosses a border, so a higher speed
// can't blow past a door mid-frame either.
u8 Player_updateRoom(Player *p, bool drunkMode, u8 speed, bool doorN, bool doorE, bool doorS, bool doorW,
                      u8 doorOffsetN, u8 doorOffsetE, u8 doorOffsetS, u8 doorOffsetW);

#endif
