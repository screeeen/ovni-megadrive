#ifndef _PLAYER_H_
#define _PLAYER_H_

#include <genesis.h>

#define DIR_UP      0
#define DIR_LEFT    1
#define DIR_DOWN    2
#define DIR_RIGHT   3

typedef struct
{
    s16 x;      // pixel position, top-left of the 16x16 box
    s16 y;
    u8  dir;
} Player;

void Player_init(Player *p);

// Same "rotate 90 degrees" action bound to SPACE in the original js13k game
// (counter-clockwise: UP -> LEFT -> DOWN -> RIGHT -> UP).
void Player_rotateCCW(Player *p);

// Opposite turn, not present in the original: clockwise (UP -> RIGHT -> DOWN -> LEFT -> UP).
void Player_rotateCW(Player *p);

// Advances the player one step and resolves collisions against the maze.
// Returns TRUE when the player reaches the bottom of the maze (win condition).
bool Player_update(Player *p);

#endif
