// GENERADO POR tools/maze_tiles.py -- no editar a mano.
// Las teselas de res/sprite/maze_tiles_auto.png. Se genera a la vez que el PNG
// para que la cuenta no pueda descuadrarse con el recurso.
#ifndef _MAZE_TILES_GEN_H_
#define _MAZE_TILES_GEN_H_

#define MAZE_TILE_COUNT 7

#define TILE_SUELO  0
#define TILE_LISO   1
#define TILE_FILO   2   // linea clara abajo: debajo se ve suelo
#define TILE_CANTO  3   // canto a la derecha: a la derecha se ve suelo

// Las del arte. Van repintadas a cuerpo/sombra/brillo, asi que
// Maze_setPlanetTint las tine igual que las de a mano.
#define FILO_VAR_BASE    4
#define FILO_VAR_COUNT   1
#define CANTO_VAR_BASE   5
#define CANTO_VAR_COUNT  2

// Las de canto son reborde IZQUIERDO del arte: se dibujan volteadas.

#endif // _MAZE_TILES_GEN_H_
