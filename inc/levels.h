#ifndef _LEVELS_H_
#define _LEVELS_H_

#include <genesis.h>

// EL FORMATO DE LOS NIVELES HORNEADOS (res/levels.bin, lo escribe
// tools/levelgen). Esta aqui y no repartido entre maze.c, plants.c y el
// generador para que los tres no puedan desincronizarse: el dia que cambie un
// campo, cambia en un sitio.
//
// Fichero:
//   0   'O' 'V' 'N' 'I'
//   4   version (1)
//   5   cuantos planetas en total
//   6   cuantas galaxias
//   7   cuantos planetas lleva cada galaxia
//   8   un u32 big-endian por planeta: el offset de su registro
//
// UNA GALAXIA son los 8 planetas que ensena el menu, uno por tamano. Los
// planetas van en orden de galaxia, asi que el de (galaxia, preset) esta en el
// indice galaxia * planetasPorGalaxia + preset.
//
// Registro de planeta (26 B y luego dos tablas):
//   0   preset   1 cols   2 rows   3 letras
//   4   inicio col,row    6 meta col,row
//   8   enlace de extraccion: col, row, dir, offset
//   12  las celdas de las letras: 5 columnas y 5 filas
//   22  u32 con el offset de la sala de extraccion
//   26  mapa de bits de que celdas son sala, en orden row*cols+col
//   ..  un u32 con el offset de cada sala, en ese mismo orden
//
// Los offsets de sala son u32 y ABSOLUTOS del arranque del fichero. En u16
// relativos al payload no cabian: el techo eran 64 KB, o sea unas 950 salas.
//
// Registro de sala (11 B y luego plantas y celdas):
//   0   u16 tileSeed -- de el salen las tramas de muro, que no se guardan
//   2   ancho, alto
//   4   hub col, row (derivables de ancho/alto: van como comprobacion)
//   6   mascara de puertas, un bit por DOOR_N/E/S/W
//   7   el offset de cada una de las cuatro (solo vale donde la mascara manda)
//   11  cuantas plantas lleva cada linea
//   ..  col,row de cada planta, en orden de linea
//   ..  dos bits por celda, en orden row*ancho+col
//
// Las celdas van a dos bits porque es lo unico que Maze_draw necesita saber:
#define LVL_NADA  0   // no se dibuja (recorte de la silueta; sigue siendo muro)
#define LVL_SUELO 1
#define LVL_MURO  2

#define LVL_PLANT_LINES 3   // tiene que valer PLANTS_LINES_PER_ROOM
#define LVL_ROOM_META   11
#define LVL_PLANET_META 26

// La rejilla se hornea con TODAS las puertas cerradas, igual que la dejaba la
// pasada de cartografia: al cargar se abren las que toquen. Asi el cierre por
// letras sigue siendo progreso de partida y no tallado.

static inline u16 Levels_u16(const u8 *p) { return (u16) (p[0] | (p[1] << 8)); }

// Donde esta cada cosa del registro de planeta. Los dos que faltan (el mapa de
// bits y la tabla de offsets) son de largo variable y salen de los accesores.
#define LVL_P_PRESET   0
#define LVL_P_COLS     1
#define LVL_P_ROWS     2
#define LVL_P_LETRAS   3
#define LVL_P_INICIO   4
#define LVL_P_META     6
#define LVL_P_ENLACE   8
#define LVL_P_LETRA_X  12
#define LVL_P_LETRA_Y  17
#define LVL_P_EXTRACC  22

static inline u32 Levels_be32(const u8 *p)
{
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
}

static inline bool Levels_ok(const u8 *bin)
{
    return (bin[0] == 'O') && (bin[1] == 'V') && (bin[2] == 'N') &&
           (bin[3] == 'I') && (bin[4] == 1);
}

static inline u8 Levels_planetCount(const u8 *bin) { return bin[5]; }

static inline u8 Levels_galaxyCount(const u8 *bin)     { return bin[6]; }
static inline u8 Levels_planetsPerGalaxy(const u8 *bin) { return bin[7]; }

static inline const u8 *Levels_planet(const u8 *bin, u8 i)
{
    return bin + Levels_be32(&bin[8 + (i * 4)]);
}

// El planeta de ese tamano en esa galaxia. Es lo que elige el menu: la galaxia
// por el eje vertical, el tamano por el horizontal.
static inline const u8 *Levels_planetIn(const u8 *bin, u8 galaxia, u8 preset)
{
    return Levels_planet(bin, (u8) ((galaxia * Levels_planetsPerGalaxy(bin)) + preset));
}

static inline u8 Levels_planetPreset(const u8 *pl) { return pl[LVL_P_PRESET]; }
static inline u8 Levels_planetCols(const u8 *pl)   { return pl[LVL_P_COLS]; }
static inline u8 Levels_planetRows(const u8 *pl)   { return pl[LVL_P_ROWS]; }

static inline const u8 *Levels_planetBitmap(const u8 *pl) { return pl + LVL_PLANET_META; }

static inline const u8 *Levels_planetRoomOffsets(const u8 *pl)
{
    const u16 n = (u16) (Levels_planetCols(pl) * Levels_planetRows(pl));

    return Levels_planetBitmap(pl) + ((n + 7) >> 3);
}

static inline bool Levels_isRoom(const u8 *pl, u8 col, u8 row)
{
    const u16 n = (u16) ((row * Levels_planetCols(pl)) + col);

    return ((Levels_planetBitmap(pl)[n >> 3] >> (n & 7)) & 1) ? TRUE : FALSE;
}

// El registro de (col,row), o NULL si ahi no hay sala. El indice en la tabla
// de offsets es cuantas salas van por delante en el mapa de bits.
static inline const u8 *Levels_room(const u8 *bin, const u8 *pl, u8 col, u8 row)
{
    const u16 hasta = (u16) ((row * Levels_planetCols(pl)) + col);
    const u8 *bits = Levels_planetBitmap(pl);
    u16 n = 0, i;

    if (!Levels_isRoom(pl, col, row))
        return NULL;

    for (i = 0; i < hasta; i++)
        if ((bits[i >> 3] >> (i & 7)) & 1)
            n++;

    return bin + Levels_be32(&Levels_planetRoomOffsets(pl)[n * 4]);
}

// La de extraccion, que vive fuera de la rejilla y tiene su propia ranura.
static inline const u8 *Levels_extractionRoom(const u8 *bin, const u8 *pl)
{
    return bin + Levels_be32(&pl[LVL_P_EXTRACC]);
}

static inline u8 Levels_roomPlantCount(const u8 *sala)
{
    return (u8) (sala[LVL_ROOM_META] + sala[LVL_ROOM_META + 1] + sala[LVL_ROOM_META + 2]);
}

static inline const u8 *Levels_roomPlants(const u8 *sala)
{
    return &sala[LVL_ROOM_META + LVL_PLANT_LINES];
}

static inline const u8 *Levels_roomCells(const u8 *sala)
{
    return Levels_roomPlants(sala) + (Levels_roomPlantCount(sala) * 2);
}

static inline u8 Levels_cell(const u8 *celdas, u8 w, s16 x, s16 y)
{
    const u16 n = (u16) ((y * w) + x);

    return (u8) ((celdas[n >> 2] >> ((n & 3) * 2)) & 3);
}

#endif
