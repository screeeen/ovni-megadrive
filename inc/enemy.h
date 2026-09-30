#ifndef _ENEMY_H_
#define _ENEMY_H_

#include <genesis.h>

// One enemy per room (user request: "en cada habitación 1 enemigo"),
// placeholder sprite (enemyShip, already in resources.res). It patrols a
// single straight lane hugging one of the room's own walls -- one border
// row/column, picked at spawn -- sliding back and forth along it and
// bouncing off whatever it runs into (the room's own corners, an
// obstacle, a sealed door), same collision-driven bounce player.c's own
// movePlayer() uses. The axis never changes after spawn: a
// DIR_UP/DIR_DOWN enemy only ever flips between those two, a
// DIR_LEFT/DIR_RIGHT one only between those.
//
// Two states, cycling on a timer (user request, placeholder distinction --
// main.c tints the shared sprite by state, no separate art):
// ENEMY_DANGEROUS kills the player on contact; ENEMY_VULNERABLE lets the
// player kill IT on contact instead ("atropellarlo", melee by running
// into it). Toggles every ENEMY_STATE_FRAMES regardless of the player --
// not triggered by anything the player does.
typedef enum { ENEMY_DANGEROUS, ENEMY_VULNERABLE } EnemyState;

// How long each state lasts (user request: "parpadea solo, por tiempo").
#define ENEMY_STATE_FRAMES 150 // 2.5s at 60fps

// How many frames a killed enemy blinks before actually disappearing
// (placeholder "death animation" -- user request, no real art needed).
#define ENEMY_DEATH_BLINK_FRAMES 24
#define ENEMY_DEATH_BLINK_PERIOD 4 // toggle visibility every this many frames

typedef struct
{
    s16 x;              // pixel position, top-left of the 16x16 box -- same convention as Player
    s16 y;
    u8 dir;             // DIR_UP/DOWN/LEFT/RIGHT (player.h) -- fixed axis, only ever flips to its opposite
    EnemyState state;
    u16 stateTimer;     // counts up to ENEMY_STATE_FRAMES, then flips state and resets
    bool alive;         // FALSE once the player has killed it (melee, spec below) -- stops updating/colliding
    u16 deathTimer;      // counts up during the death blink; alive stays TRUE until it reaches 0 (see Enemy_update)
} Enemy;

// Picks the patrol lane (one of the 4 border-adjacent rows/columns) and a
// starting position along it deterministically from roomSeed -- same
// persistence philosophy as the room's own layout (Maze_generateRoom):
// same seed, same enemy, every time this room loads. Always spawns alive,
// ENEMY_DANGEROUS, timer reset. Caller must have already generated the
// room's maze (this searches Maze_isWall for an open starting cell along
// the chosen lane).
// doorN/E/S/W and doorOffsets[4] (spec §30, same values/index convention
// maze.c's own Maze_generateRoom takes -- 0=N,1=E,2=S,3=W -- passed
// straight through rather than #including guidemap.h here, same reasoning
// as maze.h's own doorDir comment) describe this room's active doors: user
// request "un enemigo no puede estar en la trayectoria de entrada o salida
// de una room" -- for each active door this traces the straight line a
// ship slides along right after crossing it (and, symmetrically, right
// before leaving through it) out to the first wall, and keeps the enemy's
// entire patrol -- not just its spawn point -- off every cell on any of
// those lines, so it can never be sitting exactly where the player is
// about to arrive or has to stand to leave. See enemy.c's own
// markDoorTrajectory.
void Enemy_spawnForRoom(Enemy *e, u16 roomSeed, bool doorN, bool doorE, bool doorS, bool doorW,
                         const u8 doorOffsets[4]);

// Advances the enemy one pixel along its fixed axis, bouncing on
// collision, and ticks its state/death timers. No-op once e->alive is
// FALSE and its death blink has finished (see Enemy_isGone).
void Enemy_update(Enemy *e);

// Starts the death blink (user request: melee-killed enemies get a
// placeholder animation, not just an instant vanish) -- e->alive stays
// TRUE while it plays (Enemy_update keeps ticking deathTimer down),
// Enemy_isGone() flips TRUE once it's over. Stops movement/state-cycling
// immediately (Enemy_update's own early-out).
void Enemy_kill(Enemy *e);

// TRUE once a killed enemy's death blink has finished -- main.c hides its
// sprite for good at that point instead of every frame during the blink
// (which toggles visibility itself, see Enemy_blinkVisible).
bool Enemy_isGone(const Enemy *e);

// TRUE if the enemy should be drawn as visible THIS frame -- always TRUE
// while alive and not dying, toggles every ENEMY_DEATH_BLINK_PERIOD
// frames during the death blink. Meaningless once Enemy_isGone() is TRUE.
bool Enemy_blinkVisible(const Enemy *e);

// Same 16x16 AABB overlap test as Items_tryCollect uses for the letter --
// the enemy "sprite" is checked at the same box size as everything else
// in this game.
bool Enemy_overlapsBox(const Enemy *e, s16 boxX, s16 boxY);

#endif
