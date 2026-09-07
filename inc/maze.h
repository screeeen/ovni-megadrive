#ifndef _MAZE_H_
#define _MAZE_H_

#include <genesis.h>

// One maze cell = 16x16 px = 2x2 VDP background tiles.
// 20x14 cells * 16px = 320x224px, exactly one Mega Drive screen (no scrolling needed).
#define MAZE_TILE_PX    16
#define MAZE_W          20
#define MAZE_H          14

// Uploads the maze tileset to VRAM and sets its palette. Call once at boot.
void Maze_loadGraphics(void);

// Carves a new maze (recursive-backtracker, ported from ovni's generateMap.js)
// and leaves it ready to draw.
void Maze_generate(void);

// Draws the current maze to plane BG_A.
void Maze_draw(void);

// tx/ty in maze-cell units. Out-of-range coordinates count as wall.
bool Maze_isWall(s16 tx, s16 ty);

s16 Maze_startPixelX(void);
s16 Maze_startPixelY(void);

#endif
