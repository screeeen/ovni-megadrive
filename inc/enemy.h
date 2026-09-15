#ifndef _ENEMY_H_
#define _ENEMY_H_

#include <genesis.h>

typedef struct
{
    s16 x;      // pixel position, top-left of the 16x16 box
    s16 y;
    u8  dir;    // DIR_UP/DOWN/LEFT/RIGHT (player.h)
} Enemy;

// Places the enemy at a random open tile in the room (retried against the
// live random() stream, already reseeded from roomSeed by
// Maze_generateRoom just before this runs -- so it's still deterministic
// per room, same persistence philosophy as the room layout itself, spec
// §5) and picks a starting direction from roomSeed.
void Enemy_spawnForRoom(Enemy *e, u16 roomSeed);

// Advances the enemy one step along its fixed axis (spec §25): straight
// travel either vertical (UP/DOWN) or horizontal (LEFT/RIGHT), whichever
// Enemy_spawnForRoom happened to pick, bouncing back the way it came the
// instant it hits a wall -- same pixel-level box collision player.c's
// movePlayer() uses. Never turns onto the other axis; DIR_UP only ever
// flips to DIR_DOWN and back, DIR_LEFT only to DIR_RIGHT and back. This
// replaces the earlier "turn right, then left, then reverse" pathing
// (spec §12) -- simpler by request, and sidesteps that whole family of
// stuck-in-a-loop concerns since there's no turning decision left to get
// stuck in. No player interaction yet (collision with the ship is
// explicitly out of scope for now).
void Enemy_update(Enemy *e);

// True if the two enemies' 16x16 boxes overlap -- for enemy-vs-enemy
// collision only (main.c decides what to do about it, e.g. reversing
// both via Enemy_opposite). Not used for player collision, still out of
// scope.
bool Enemy_overlaps(const Enemy *a, const Enemy *b);

// DIR_UP<->DIR_DOWN, DIR_LEFT<->DIR_RIGHT.
u8 Enemy_opposite(u8 dir);

#endif
