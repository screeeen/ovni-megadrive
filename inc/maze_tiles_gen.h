// GENERADO POR tools/maze_tiles16.py -- no editar a mano.
// Las celdas de res/sprite/maze_tiles16.png. Se genera a la vez que el PNG para
// que la cuenta no pueda descuadrarse con el recurso.
#ifndef _MAZE_TILES_GEN_H_
#define _MAZE_TILES_GEN_H_

// Teselas de 8x8 que ocupa la hoja entera en VRAM, y las que hay en una fila
// de 8 px de la hoja: la celda c son las teselas 2c y 2c+1 arriba y
// MAZE_SHEET_ROW + 2c y +1 abajo (ver Maze_draw).
#define MAZE_TILE_COUNT  44
#define MAZE_SHEET_ROW   22

// Valores del grid: el suelo es el 0, las tramas de muro van seguidas y la
// maciza no la pone el generador (la usa la barra de progreso).
#define CELL_SUELO       0
#define CELL_MURO_BASE   1
#define CELL_MURO_COUNT  9
#define CELL_SOLIDO      10

// Teselas sueltas de 8x8, para lo que no dibuja celdas enteras.
#define TILE_SUELO       0
#define TILE_SOLIDO      20

#endif // _MAZE_TILES_GEN_H_
