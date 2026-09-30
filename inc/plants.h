#ifndef _PLANTS_H_
#define _PLANTS_H_

#include <genesis.h>

// Placeholder collectible plants (user request): "un tipo de planta que
// quepa en 1 tile... cuando la nave pasa por ellas las recoge y suma un
// contador... distribúyelas en líneas dentro del path del grafo... se
// colocan en grupos de varias en línea." A plant is one maze cell, drawn
// as a plain text glyph -- explicitly a placeholder, no real art, same
// "reuse VDP_drawText" shortcut items.c's own letters already use.
// Unlike items.h's letters, there's no order/locking -- just a running
// counter, and plants never gate anything.
//
// Placed in ONE line per room, several consecutive cells along a
// straight run of open (non-wall) interior -- approximated with
// Maze_isWall() scans rather than literally maze.c's internal tombo
// slide-graph (that graph's nodes aren't exposed outside maze.c, see
// Maze_drawDebugGraph's own doc comment), which in practice is the same
// walkable area since tombo rooms are mostly open interior (maze.h).

// Clears every room's collected-plant state and the running total. Call
// once per newGame(), alongside Items_reset().
void Plants_reset(void);

// Regenerates this room's plant line deterministically from roomSeed --
// own local PRNG, independent of the shared random() stream (see
// plants.c's own doc comment: Maze_generateRoom's accepted-attempt cache
// can replay a cached layout without repeating its original search, so
// the shared stream's state after it returns isn't reliably the same
// every visit -- same reasoning enemy.c avoids it for). Same persistence
// philosophy as the room's own layout: same roomSeed, same line, every
// time. Call right after Maze_generateRoom() in loadRoom(), before
// Maze_draw(). Not called for the insertion room (no plants there, same
// as enemy.c).
void Plants_spawnForRoom(u16 roomSeed);

// Player is in room (col,row), ship box at pixel (playerX,playerY).
// Marks any of this room's plants the ship's 16x16 box newly overlaps as
// collected (persisted per-room) and bumps the running total. Returns
// TRUE if at least one was collected this call (caller should redraw:
// Maze_draw() + Plants_drawInRoom() + Plants_drawHud(), same pattern a
// letter pickup already uses).
bool Plants_tryCollect(u8 col, u8 row, s16 playerX, s16 playerY);

// Draws every still-uncollected plant of THIS room (the one
// Plants_spawnForRoom last ran for) at its fixed cell position. Call
// after Maze_draw() -- like Items_drawInRoom, this only touches those
// tiles, not the rest of the plane. col/row select which room's
// collected-state to check (the caller's current room).
void Plants_drawInRoom(u8 col, u8 row);

// Draws the running total ("PLANTAS: N") on BG_B row 1, to the right of
// Items_drawHud's own letter tracker -- same plane/row/high-priority
// trick. Call once after Plants_reset() and again every time
// Plants_tryCollect() returns TRUE.
void Plants_drawHud(void);

#endif
