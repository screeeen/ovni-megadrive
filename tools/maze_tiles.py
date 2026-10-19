#!/usr/bin/env python3
"""Arma res/sprite/maze_tiles_auto.png: las 4 piezas a mano mas las del arte.

Las de suelo van con su color (indices 6..14 de PAL0, que es el colormap del
PNG). Las de muro se repintan a cuerpo/sombra/brillo para que Maze_setPlanetTint
las siga tinendo. Editar SUELO/FILO/CANTO de abajo y volver a correrlo.
"""
import collections, os, struct, subprocess, sys, zlib

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARTE = os.path.join(RAIZ, "res/sprite/maze_tiles_90.png")
BASE = os.path.join(RAIZ, "res/sprite/maze_tiles_base.png")
SAL  = os.path.join(RAIZ, "res/sprite/maze_tiles_auto.png")
GEN  = os.path.join(RAIZ, "inc/maze_tiles_gen.h")
AW, AH = 216, 384

# Los ids son los de clasificacion_tiles.txt y los de la hoja clicable.
# Sin variantes de suelo (peticion del usuario): el suelo es el plano de
# maze_tiles_base.png y nada mas. Anadir ids aqui las devuelve.
SUELO = []
# La 74 fuera: repintada a cuerpo/sombra/brillo sale byte a byte igual al filo
# de maze_tiles_base.png, asi que solo gastaria VRAM.
FILO  = [1]
CANTO = [8, 77]   # son reborde IZQUIERDO: Maze_draw las voltea

PAL_SUELO, PAL_CUERPO, PAL_SOMBRA, PAL_BRILLO = 2, 3, 4, 5
# Los dos tonos del suelo. Los escribe Maze_setPlanetTint, un nivel del VDP por
# encima y por debajo del suelo de siempre: la textura tiene que notarse apenas.
PAL_SUELO_A, PAL_SUELO_B = 6, 7


def md(v):
    """El nivel que le toca a este canal en el VDP, que son 3 bits.

    Tal como lo hace RGB24_TO_VDPCOLOR de SGDK: suma 16 y trunca. No es
    redondear -- con v=49 redondear da 1 y el chip da 2 -- y de eso depende
    que dos colores del arte acaben siendo el mismo en pantalla o no.
    """
    return min((v + 16) >> 5, 7)


def leer_rgb(ruta, w, h):
    d = subprocess.run(["magick", ruta, "-depth", "8", "rgb:-"],
                       capture_output=True, check=True).stdout
    if len(d) != w * h * 3:
        sys.exit("%s no mide %dx%d" % (ruta, w, h))
    return d


def teselas_del_arte():
    raw = leer_rgb(ARTE, AW, AH)
    vistas, orden = {}, []
    for cy in range(AH // 8):
        for cx in range(AW // 8):
            c = b"".join(raw[(y * AW + cx * 8) * 3:(y * AW + cx * 8) * 3 + 24]
                         for y in range(cy * 8, cy * 8 + 8))
            if c not in vistas:
                vistas[c] = len(orden)
                orden.append(c)
    return orden


def pixeles(tesela):
    return [tuple(md(tesela[i * 3 + k]) for k in range(3)) for i in range(64)]


def indices_de_suelo(px):
    """Lo que mas sale se queda en el suelo de siempre; el resto es la textura,
    lo claro arriba y lo oscuro abajo. Tres tonos y ninguno propio."""
    cuenta = collections.Counter(px)
    base = cuenta.most_common(1)[0][0]
    resto = sorted((c for c in cuenta if c != base), key=lambda c: sum(c))
    claro = resto[-1] if resto else None
    salida = []
    for c in px:
        if c == base:
            salida.append(PAL_SUELO)
        elif c == claro:
            salida.append(PAL_SUELO_A)
        else:
            salida.append(PAL_SUELO_B)
    return salida


def indices_de_muro(px):
    """Lo que mas sale es el cuerpo; de lo demas, lo mas claro es el brillo."""
    cuenta = collections.Counter(px)
    cuerpo = cuenta.most_common(1)[0][0]
    resto = [c for c in cuenta if c != cuerpo]
    brillo = max(resto, key=lambda c: c[0] + c[1] + c[2]) if resto else None
    salida = []
    for c in px:
        if c == cuerpo:
            salida.append(PAL_CUERPO)
        elif c == brillo:
            salida.append(PAL_BRILLO)
        else:
            salida.append(PAL_SOMBRA)
    return salida


def teselas_base():
    """Las 4 a mano, de vuelta a indices contra el colormap del propio PNG."""
    raw = leer_rgb(BASE, 32, 8)
    fijo = {(0, 0, 36): 0, (25, 23, 22): PAL_SUELO, (109, 182, 72): PAL_CUERPO,
            (36, 72, 36): PAL_SOMBRA, (218, 255, 255): PAL_BRILLO}
    fuera = []
    for t in range(4):
        px = []
        for y in range(8):
            for x in range(8):
                o = (y * 32 + t * 8 + x) * 3
                c = (raw[o], raw[o + 1], raw[o + 2])
                if c not in fijo:
                    sys.exit("color %s en maze_tiles_base.png que no es de PAL0" % (c,))
                px.append(fijo[c])
        fuera.append(px)
    return fuera


def escribir_png(ruta, teselas, colormap):
    n = len(teselas)
    w, h = n * 8, 8
    filas = b""
    for y in range(8):
        linea = bytes(teselas[t][y * 8 + x] for t in range(n) for x in range(8))
        filas += b"\x00" + linea
    plte = b"".join(bytes(c) for c in colormap)

    def trozo(tipo, dat):
        c = tipo + dat
        return struct.pack(">I", len(dat)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    with open(ruta, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(trozo(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)))
        f.write(trozo(b"PLTE", plte))
        f.write(trozo(b"IDAT", zlib.compress(filas, 9)))
        f.write(trozo(b"IEND", b""))
    return w, h


def main():
    arte = teselas_del_arte()

    tiras = teselas_base()
    planas = []
    for t in SUELO:
        px = indices_de_suelo(pixeles(arte[t]))
        if len(set(px)) == 1:
            planas.append(t)
        tiras.append(px)
    for t in FILO + CANTO:
        tiras.append(indices_de_muro(pixeles(arte[t])))

    colormap = [(0, 0, 36), (152, 125, 250), (25, 23, 22), (109, 182, 72),
                (36, 72, 36), (218, 255, 255)] + [(0, 0, 0)] * 10
    # Lo que Maze_setPlanetTint va a escribir encima; aqui solo tienen que ser
    # distintos entre si para que el PNG no funda los tres tonos en uno.
    colormap[PAL_SUELO_A] = (73, 73, 73)
    colormap[PAL_SUELO_B] = (0, 0, 0)

    w, h = escribir_png(SAL, tiras, colormap)
    base_suelo = 4
    base_filo = base_suelo + len(SUELO)
    base_canto = base_filo + len(FILO)
    escribir_h(len(tiras), base_filo, base_canto)

    print("%s: %dx%d, %d teselas" % (os.path.relpath(SAL, RAIZ), w, h, len(tiras)))
    print("  0..3    las 4 a mano")
    if SUELO:
        print("  %d..%-3d %d de suelo %s" % (base_suelo, base_filo - 1, len(SUELO), SUELO))
    print("  %d..%-3d filo %s" % (base_filo, base_canto - 1, FILO))
    print("  %d..%-3d canto %s (Maze_draw las voltea)"
          % (base_canto, len(tiras) - 1, CANTO))
    if planas:
        print("  sin textura (un solo tono, salen como el suelo de siempre): %s" % planas)


def escribir_h(total, base_filo, base_canto):
    with open(GEN, "w") as f:
        f.write("""// GENERADO POR tools/maze_tiles.py -- no editar a mano.
// Las teselas de res/sprite/maze_tiles_auto.png. Se genera a la vez que el PNG
// para que la cuenta no pueda descuadrarse con el recurso.
#ifndef _MAZE_TILES_GEN_H_
#define _MAZE_TILES_GEN_H_

#define MAZE_TILE_COUNT %d

#define TILE_SUELO  0
#define TILE_LISO   1
#define TILE_FILO   2   // linea clara abajo: debajo se ve suelo
#define TILE_CANTO  3   // canto a la derecha: a la derecha se ve suelo

// Las del arte. Van repintadas a cuerpo/sombra/brillo, asi que
// Maze_setPlanetTint las tine igual que las de a mano.
#define FILO_VAR_BASE    %d
#define FILO_VAR_COUNT   %d
#define CANTO_VAR_BASE   %d
#define CANTO_VAR_COUNT  %d

// Las de canto son reborde IZQUIERDO del arte: se dibujan volteadas.

#endif // _MAZE_TILES_GEN_H_
""" % (total, base_filo, len(FILO), base_canto, len(CANTO)))


main()
