#!/usr/bin/env python3
"""Arma res/sprite/maze_tiles16.png: la hoja del laberinto con celda de 16x16.

Es el arte de siempre -- las 9 tramas de 16x16 de res/sprite/maze_tiles_dither.png,
las del ovni original -- repintadas a los indices de PAL0 que usa maze.c: la trama
en PAL_CUERPO y sus huecos en PAL_SUELO, el mismo del suelo. El suelo no viene del
arte, es una celda maciza de ese mismo indice, asi que Maze_setPlanetTint tine la
sala entera moviendo dos entradas de paleta.

La hoja es una fila de celdas de 16x16 y rescomp la trocea en teselas de 8x8 por
filas (TILESET ... ROW, sin dedup), asi que la celda c ocupa las teselas
2c y 2c+1 arriba y FILA+2c, FILA+2c+1 abajo, con FILA = teselas de una fila.
"""
import os, struct, sys, zlib

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARTE = os.path.join(RAIZ, "res/sprite/maze_tiles_dither.png")
SAL  = os.path.join(RAIZ, "res/sprite/maze_tiles16.png")
GEN  = os.path.join(RAIZ, "inc/maze_tiles_gen.h")

PAL_SUELO, PAL_CUERPO = 2, 3
N = 16                  # lado de la celda
MUROS = range(1, 10)    # las 9 tramas del arte; la 0 es el suelo vacio


def leer_indexado(ruta):
    d = open(ruta, "rb").read()
    i, idat, w, h = 8, b"", 0, 0
    while i < len(d):
        ln = struct.unpack(">I", d[i:i + 4])[0]
        t, dat = d[i + 4:i + 8], d[i + 8:i + 8 + ln]
        if t == b"IHDR":
            w, h, prof, tipo = struct.unpack(">IIBB", dat[:10])
            if (prof, tipo) != (8, 3):
                sys.exit("%s no es indexado de 8 bits" % ruta)
        if t == b"IDAT":
            idat += dat
        i += 12 + ln
    raw = zlib.decompress(idat)
    if any(raw[y * (w + 1)] for y in range(h)):
        sys.exit("%s trae filtros PNG: vuelve a guardarlo sin ellos" % ruta)
    return w, h, [list(raw[y * (w + 1) + 1:(y + 1) * (w + 1)]) for y in range(h)]


def trozo(tipo, dat):
    c = tipo + dat
    return struct.pack(">I", len(dat)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)


def escribir_png(ruta, celdas, paleta):
    w, h = len(celdas) * N, N
    filas = b""
    for y in range(N):
        filas += b"\x00" + bytes(c[y][x] for c in celdas for x in range(N))
    with open(ruta, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(trozo(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)))
        f.write(trozo(b"PLTE", b"".join(bytes(c) for c in paleta)))
        f.write(trozo(b"IDAT", zlib.compress(filas, 9)))
        f.write(trozo(b"IEND", b""))
    return w, h


def escribir_h(celdas, fila_teselas):
    with open(GEN, "w") as f:
        f.write("""// GENERADO POR tools/maze_tiles16.py -- no editar a mano.
// Las celdas de res/sprite/maze_tiles16.png. Se genera a la vez que el PNG para
// que la cuenta no pueda descuadrarse con el recurso.
#ifndef _MAZE_TILES_GEN_H_
#define _MAZE_TILES_GEN_H_

// Teselas de 8x8 que ocupa la hoja entera en VRAM, y las que hay en una fila
// de 8 px de la hoja: la celda c son las teselas 2c y 2c+1 arriba y
// MAZE_SHEET_ROW + 2c y +1 abajo (ver Maze_draw).
#define MAZE_TILE_COUNT  %d
#define MAZE_SHEET_ROW   %d

// Valores del grid: el suelo es el 0, las tramas de muro van seguidas y la
// maciza no la pone el generador (la usa la barra de progreso).
#define CELL_SUELO       %d
#define CELL_MURO_BASE   %d
#define CELL_MURO_COUNT  %d
#define CELL_SOLIDO      %d

// Teselas sueltas de 8x8, para lo que no dibuja celdas enteras.
#define TILE_SUELO       %d
#define TILE_SOLIDO      %d

#endif // _MAZE_TILES_GEN_H_
""" % (celdas * 4, fila_teselas, 0, 1, len(MUROS), 1 + len(MUROS),
       0, 2 * (1 + len(MUROS))))


def main():
    w, h, px = leer_indexado(ARTE)
    if (w, h) != (160, 16):
        sys.exit("%s no mide 160x16" % ARTE)

    celdas = [[[PAL_SUELO] * N for _ in range(N)]]          # 0: suelo macizo
    for c in MUROS:
        # Los huecos de la trama son suelo, no fondo: el muro se apoya en el
        # suelo del planeta en vez de abrir un agujero de otro color.
        celdas.append([[PAL_CUERPO if px[y][c * N + x] else PAL_SUELO for x in range(N)]
                       for y in range(N)])
    celdas.append([[PAL_CUERPO] * N for _ in range(N)])     # la maciza

    # Lo que escribe encima Maze_setPlanetTint; aqui solo tienen que ser
    # distintos entre si para que el PNG no funda los dos en uno.
    paleta = [(0, 0, 36), (152, 125, 250), (25, 23, 22), (109, 182, 72)] + [(0, 0, 0)] * 12

    ancho, alto = escribir_png(SAL, celdas, paleta)
    escribir_h(len(celdas), ancho // 8)
    print("%s: %dx%d, %d celdas de %d, %d teselas"
          % (os.path.relpath(SAL, RAIZ), ancho, alto, len(celdas), N, len(celdas) * 4))


main()
