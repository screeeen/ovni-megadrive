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
// Placed in PLANTS_LINES_PER_ROOM (plants.c, 3 -- 1 original + 2 more,
// user request: "añade dos hileras más de plantas") independent lines
// per room, each several consecutive cells along a real EDGE of maze.c's
// own tombo slide graph (user request, tightened after an earlier "any
// straight run of open cells" approximation missed that an open-but-
// unreachable pocket could still pass that check: "las lineas de
// plantas tienen que estar en una linea del grafo accesible para la
// nave. Todas las plantas son susceptibles de ser cogidas.") --
// maze.c's Maze_slideNodeCount/Pos/Edge expose that same graph
// Maze_drawDebugGraph() already draws dots for, so every cell a plant
// sits on is guaranteed to be one the ship's own slide actually crosses.
// The 3 lines never share a cell (plants.c's own occupancy tracking
// while spawning).

// Clears every room's collected-plant state and the running total. Call
// once per newGame(), alongside Items_reset().
void Plants_reset(void);

// Regenerates this room's plant lines deterministically from roomSeed --
// own local PRNG, independent of the shared random() stream (see
// plants.c's own doc comment: Maze_generateRoom's accepted-attempt cache
// can replay a cached layout without repeating its original search, so
// the shared stream's state after it returns isn't reliably the same
// every visit -- same reasoning enemy.c avoids it for). Same persistence
// philosophy as the room's own layout: same roomSeed, same lines, every
// time. Call right after Maze_generateRoom() in loadRoom(), before
// Maze_draw(). Not called for the insertion room (no plants there, same
// as enemy.c).
void Plants_spawnForRoom(u16 roomSeed);

// Player is in room (col,row), ship box at pixel (playerX,playerY).
// Marks any of this room's plants (any of its PLANTS_LINES_PER_ROOM
// lines) the ship's 16x16 box newly overlaps as collected (persisted
// per-room) and bumps the running total. Returns TRUE if at least one
// was collected this call (caller should redraw: Maze_draw() +
// Plants_drawInRoom() + Plants_drawHud(), same pattern a letter pickup
// already uses).
bool Plants_tryCollect(u8 col, u8 row, s16 playerX, s16 playerY);

// Draws every still-uncollected plant of THIS room (the one
// Plants_spawnForRoom last ran for), across all its lines, at each
// one's fixed cell position. Call after Maze_draw() -- like
// Items_drawInRoom, this only touches those tiles, not the rest of the
// plane. col/row select which room's collected-state to check (the
// caller's current room).
void Plants_drawInRoom(u8 col, u8 row);

// Draws the running total ("PLANTAS: N") on BG_B row 1, to the right of
// Items_drawHud's own letter tracker -- same plane/row/high-priority
// trick. Call once after Plants_reset() and again every time
// Plants_tryCollect() returns TRUE.
void Plants_drawHud(void);

// How many plants have been collected so far THIS session (spec §51) --
// same value Plants_drawHud() prints. main.c saves this into
// presetSave[].plantsCollected on resetToMenu(), same pattern
// Items_collectedCount() already has for letters.
u16 Plants_collectedCount(void);

// Restores a saved running total instantly (spec §51, resuming a
// planet) -- unlike Items_fastForward, there's no order/position to
// replay, a plant pickup is just a counter, so this is a plain setter.
// Call right after Plants_reset(), before any room loads.
void Plants_setCollected(u16 count);

// Total plants in whichever room Plants_spawnForRoom last ran for,
// summed across all PLANTS_LINES_PER_ROOM lines (spec §51: main.c's
// scanPlanetPlantTotal() calls Plants_spawnForRoom() once per room in a
// saved planet's map and adds this up, to show "collected / total" in
// the menu -- a planet's plant total isn't a fixed preset constant like
// its letter count, so it has to be computed this way).
u16 Plants_lastRoomCount(void);

#endif
