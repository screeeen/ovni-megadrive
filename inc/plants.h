#ifndef _PLANTS_H_
#define _PLANTS_H_

#include <genesis.h>
#include "guidemap.h" // MAX_MAP_COLS/MAX_MAP_ROWS, for Plants_saveMask/restoreMask's array type

// Collectible plants (user request): "un tipo de planta que quepa en 1
// tile... cuando la nave pasa por ellas las recoge y suma un contador...
// distribúyelas en líneas dentro del path del grafo... se colocan en
// grupos de varias en línea." A plant is one maze cell, drawn with its
// own tile, y no hay dos iguales en todo el juego: la forma sale del
// indice unico de cada planta (ver Plants_buildTile mas abajo).
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
// Deja puestas las tres tintas de la flora en PAL0. Una vez al arrancar,
// junto a Maze_loadGraphics(). El dibujo no se carga: se arma al vuelo.
void Plants_loadGraphics(void);

// Los cuatro tonos de la flora en PAL0. Fijos en todo el juego: a las plantas
// no las tine el planeta, que es lo que hace que una se reconozca de un sitio
// a otro. El de silueta solo lo usa la coleccion.
#define PAL_PLANTA_OSCURA  11   // #490049
#define PAL_PLANTA_MEDIA   12   // #B64992
#define PAL_PLANTA_CLARA   13   // #FFB6DB
#define PAL_PLANTA_SILUETA 14   // #6D2449

// Las plantas que como mucho tiene una habitacion: el paso del indice unico
// de abajo, y las teselas que la sala reserva en VRAM.
#define PLANTS_PER_ROOM 15

// La primera tesela libre detras de las de las plantas de la sala, para quien
// quiera cargar las suyas ahi -- la pagina de coleccion del menu.
u16 Plants_tilesEnd(void);

// EL INDICE UNICO DE UNA PLANTA. Cada planta del juego tiene el suyo:
//
//     indice = slot * PLANTS_PER_ROOM + linea * PLANTS_MAX_PER_LINE + i
//
// donde slot es el que main.c reparte por (planeta, col, fila). De ahi sale su
// forma, y no hay dos iguales en todo el juego: el indice decide perfil,
// relleno y tallo por restos de tres modulos coprimos, y tools/plant_tiles.py
// comprueba al generar que las PLANT_SHAPE_COUNT combinaciones dan
// PLANT_SHAPE_COUNT dibujos distintos.
//
// Que huecos de esa habitacion son plantas de verdad (una habitacion tiene
// entre 3 y PLANTS_PER_ROOM), un bit por hueco igual que la mascara de
// recogidas. 0 cuando el planeta no se ha cartografiado todavia.
u16 Plants_cachedRoomMask(u16 slot);

// Solo la marca de la retícula: la casilla que no le toca a ninguna planta.
void Plants_buildSpotTile(u8 *dst);

// La planta tal como la pinta la COLECCION: con la marca de su spot, y en un
// solo tono apagado mientras no se haya recogido, para que se vea que forma
// falta. Plants_buildTile de arriba es la del laberinto y no lleva marca.
void Plants_buildSpot(u16 shape, u8 *dst, bool recogida);

// La silueta a secas, sin la marca del spot: para verla en grande.
void Plants_buildSilhouette(u16 shape, u8 *dst);

// El nombre de esa planta, hasta 15 letras mas el cero. Estable y propio: dos
// plantas nunca comparten nombre.
void Plants_name(u16 shape, char *out);

// Escribe en dst los 32 bytes de la tesela de esa planta. Es lo que va a
// necesitar la pagina de coleccion del menu para pintar una planta suelta.
void Plants_buildTile(u16 shape, u8 *dst);

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
void Plants_spawnForRoom(u16 roomSeed, u16 slot);

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
// Slots are per PLANET now, not per map position: the cache used to be
// indexed by (col,row) alone, so it only ever held one planet's rooms and
// whichever planet was cartographed last evicted the rest. main.c hands
// out a slot per (planet, col, row) -- packed, so only cells that are
// really rooms take space. PLANTS_CACHE_SLOTS must cover every planet's
// rooms added together; main.c checks it against the real total.
#define PLANTS_CACHE_SLOTS 171
void Plants_cacheCurrentRoom(u16 slot);
bool Plants_loadCachedRoom(u16 slot);

// TRUE if cell (x,y) of THIS room is part of any of its plant lines, no
// matter whether that plant has been collected already. Unlike
// Plants_uncollectedAt below, this is about the LAYOUT, not the state:
// enemy.c uses it to keep an enemy's patrol off a line of plants (user
// request: "un enemigo y una hilera de plantas no deben compartir la
// misma arista"), and a patrol that moved as the player collected would
// be worse than one that never shared the run in the first place.
bool Plants_cellOccupied(s16 x, s16 y);

// TRUE if cell (x,y) of THIS room still holds an uncollected plant. Same
// col/row meaning as Plants_drawInRoom. Used by main.c's "route to the
// nearest plant" debug overlay.
bool Plants_uncollectedAt(u8 roomCol, u8 roomRow, s16 x, s16 y);

#endif
