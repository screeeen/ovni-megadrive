#ifndef _PLANTS_H_
#define _PLANTS_H_

#include <genesis.h>
#include "guidemap.h" // MAX_MAP_COLS/MAX_MAP_ROWS, for Plants_saveMask/restoreMask's array type

// Placeholder collectible plants (user request): "un tipo de planta que
// quepa en 1 tile... cuando la nave pasa por ellas las recoge y suma un
// contador... distribúyelas en líneas dentro del path del grafo... se
// colocan en grupos de varias en línea." A plant is one maze cell, drawn
// as a plain text glyph -- explicitly a placeholder, no real art, same
// "reuse VDP_drawText" shortcut items.c's own letters already use.
// Unlike items.h's letters, there's no COLLECTION order -- just a
// running counter. They do gate something now, though (user request:
// "las puertas se abren en cada habitación siempre que se hayan
// recogido las plantas y la letra de la misma"): main.c's
// Plants_allCollectedInRoom() below feeds into every room's own door
// lock, alongside the letter.
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

// Draws the running total against this planet's real total ("PLANTAS:
// n/TOTAL", user request) on BG_B row 1, to the right of Items_drawHud's
// own letter tracker -- same plane/row/high-priority trick. Reads
// Plants_setPlanetTotal()'s value for the denominator, so call that
// first. Call once after Plants_reset() and again every time
// Plants_tryCollect() returns TRUE.
void Plants_drawHud(void);

// How many plants have been collected so far THIS session (spec §51) --
// same value Plants_drawHud() prints. main.c saves this into
// presetSave[].plantsCollected on resetToMenu(), same pattern
// Items_collectedCount() already has for letters.
u16 Plants_collectedCount(void);

// Restores a saved running total instantly (spec §51, resuming a
// planet) -- a plain setter, no order/position to replay the way
// Items_fastForward has to. Call right after Plants_reset(), before any
// room loads.
//
// BUG FIX (user report: "94 DE 88 PLANTAS" in the menu -- collected
// exceeding the real total). This setter alone was never enough: it
// restores the NUMBER but not which specific plants (which rooms) are
// already gone, and Plants_reset() (which must run first, to re-init
// `total` and every room's curCount/curCol/curRow) also wipes
// collectedMask -- so a resumed planet's already-cleared rooms silently
// came back with their plants still standing, and walking through them
// again kept incrementing the restored total past the real cap. Must
// always be paired with Plants_restoreMask() below, restoring the same
// snapshot resetToMenu() took with Plants_saveMask() -- that's the part
// that actually remembers which rooms are done.
void Plants_setCollected(u16 count);

// Snapshots/restores collectedMask (plants.c) whole -- the per-room
// "which of this room's plants are already gone" bits Plants_reset()
// clears and Plants_tryCollect() sets, which Plants_setCollected()'s
// raw-count approach has no way to reconstruct on its own (see its own
// updated doc comment). main.c's resetToMenu() calls Plants_saveMask()
// into presetSave[].plantsMask alongside plantsCollected; newGame() calls
// Plants_restoreMask() right after Plants_reset()+Plants_setCollected()
// when resuming a planet that has one.
void Plants_saveMask(u16 outMask[MAX_MAP_ROWS][MAX_MAP_COLS]);
void Plants_restoreMask(const u16 inMask[MAX_MAP_ROWS][MAX_MAP_COLS]);

// Sets the denominator Plants_drawHud() prints ("PLANTAS: n/TOTAL", user
// request) -- presetSave[].plantsTotal, already known by the time
// newGame() starts actual gameplay (scanPlanetPlantTotal() computed it,
// at the latest, the first time this planet was ever entered this boot).
// Call once per newGame(), any time after Plants_reset() (which doesn't
// touch this -- it's a per-planet constant, not session state).
void Plants_setPlanetTotal(u16 total);

// Total plants in whichever room Plants_spawnForRoom last ran for,
// summed across all PLANTS_LINES_PER_ROOM lines (spec §51: main.c's
// scanPlanetPlantTotal() calls Plants_spawnForRoom() once per room in a
// saved planet's map and adds this up, to show "collected / total" in
// the menu -- a planet's plant total isn't a fixed preset constant like
// its letter count, so it has to be computed this way).
u16 Plants_lastRoomCount(void);

// TRUE if every plant THIS room has (across all PLANTS_LINES_PER_ROOM
// lines) is already collected -- including the vacuous case of a room
// with none at all. col/row select which room's collected-state to
// check, same meaning as Plants_drawInRoom's own collectedMask lookup
// (trusts curCol/curRow already reflect that room, from its last
// Plants_spawnForRoom() call). Used by main.c's per-room door lock (user
// request: "las puertas se abren en cada habitación siempre que se hayan
// recogido las plantas y la letra de la misma").
bool Plants_allCollectedInRoom(u8 col, u8 row);

// Per-room layout cache (user request: crossing a door stalled for a few
// frames). The stall was almost all spawn work: placing a room's lines
// asks maze.c which cells are safe, which costs ~15x regenerating the
// room itself -- and it ran on EVERY entry, although a room's lines are
// deterministic from its roomSeed and so are identical every single time
// for the whole run.
//
// Plants_cacheCurrentRoom stores whatever Plants_spawnForRoom last
// produced under (col,row). Plants_loadCachedRoom puts it straight back
// and returns TRUE; FALSE means that room has never been cached and the
// caller still has to spawn it the slow way (once). Collected state is
// NOT part of this -- that's collectedMask, which is per room already and
// outlives any of it.
//
// Cleared by Plants_reset(), so a new planet can never read another's
// layouts: the cache is indexed by map position, and a different mapSeed
// puts completely different rooms at those same positions.
void Plants_cacheCurrentRoom(u8 col, u8 row);
bool Plants_loadCachedRoom(u8 col, u8 row);

// TRUE if cell (x,y) of THIS room still holds an uncollected plant. Same
// col/row meaning as Plants_drawInRoom. Used by main.c's "route to the
// nearest plant" debug overlay.
bool Plants_uncollectedAt(u8 roomCol, u8 roomRow, s16 x, s16 y);

#endif
