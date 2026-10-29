// El genesis.h de pega con el que maze.c y guidemap.c compilan en el host.
// Solo lo que esos dos ficheros tocan de SGDK, y de SGDK solo usan dibujo:
// quince simbolos, todos a no-op aqui. Lo unico que NO puede ser de pega es
// el azar -- la forma de cada sala es una funcion pura de su semilla a traves
// de random(), asi que esta es la copia exacta del de SGDK (src/tools.c) y es
// lo que hace que el host saque los mismos niveles que la consola.
#ifndef _GENESIS_H_STUB_
#define _GENESIS_H_STUB_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef int      bool;

#define TRUE  1
#define FALSE 0

// --- el azar, copia literal de SGDK
static u16 randbase = 0xD94B;

static inline void setRandomSeed(u16 seed)
{
    randbase = (u16) (seed ^ 0xD94B);
}

// Se llama distinto y se renombra con un define: stdlib.h ya trae un
// random() y el del host no vale, el azar tiene que ser el de la consola.
static inline u16 sgdkRandom(void)
{
    randbase ^= (u16) (randbase >> 5);
    randbase ^= (u16) (randbase << 9);
    randbase ^= (u16) (randbase >> 7);
    return randbase;
}
#define random() sgdkRandom()

// --- planos, paletas y teselas: nombres para que compile, nada mas
#define PAL0 0
#define PAL1 1
#define PAL2 2
#define PAL3 3
#define BG_A 0
#define BG_B 1
#define DMA  0
#define CPU  1

#define TILE_USER_INDEX 16
#define TILE_ATTR_FULL(pal, prio, flipV, flipH, index) \
    ((u16) ((index) | ((pal) << 13)))

typedef struct { int nada; } TileSet;
typedef struct { int nada; } Palette;

static inline void VDP_drawText(const char *s, u16 x, u16 y) { (void) s; (void) x; (void) y; }
static inline void VDP_drawTextBG(u16 p, const char *s, u16 x, u16 y) { (void) p; (void) s; (void) x; (void) y; }
static inline void VDP_clearPlane(u16 p, bool w) { (void) p; (void) w; }
static inline void VDP_setTextPalette(u16 p) { (void) p; }
static inline void VDP_setTextPriority(u16 p) { (void) p; }
static inline void VDP_setTileMapXY(u16 p, u16 t, u16 x, u16 y) { (void) p; (void) t; (void) x; (void) y; }
static inline void VDP_setTileMapDataRect(u16 p, const u16 *d, u16 x, u16 y, u16 w, u16 h, u16 pitch, u16 tm)
{ (void) p; (void) d; (void) x; (void) y; (void) w; (void) h; (void) pitch; (void) tm; }
static inline void VDP_loadTileData(const u32 *d, u16 i, u16 n, u16 tr) { (void) d; (void) i; (void) n; (void) tr; }
static inline bool VDP_loadTileSet(const TileSet *t, u16 i, bool tr) { (void) t; (void) i; (void) tr; return TRUE; }
static inline void PAL_setColor(u16 i, u16 c) { (void) i; (void) c; }

// El color del VDP: 3 bits por canal, como RGB24_TO_VDPCOLOR del de verdad.
// Aqui no se dibuja, pero el tinte del planeta se calcula igual.
#define RGB3_3_3_TO_VDPCOLOR(r, g, b) ((u16) ((((r) & 7) << 1) | (((g) & 7) << 5) | (((b) & 7) << 9)))
#define RGB24_TO_VDPCOLOR(c)     ((u16) ((((c) & 0xE00000) >> 21) << 1 | (((c) & 0xE000) >> 13) << 5 | (((c) & 0xE0) >> 5) << 9))

#endif
