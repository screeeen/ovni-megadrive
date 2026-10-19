#!/usr/bin/env python3
"""Arma res/sprite/item_icons.png: un icono de 16x16 por objeto.

Van con las tintas de las plantas (indices 11/12/13 de PAL0), asi que el sprite
se pinta con PAL0 y no gasta linea de paleta. Frame 0 la A, frame 1 la B.
"""
import os, struct, sys, zlib

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAL = os.path.join(RAIZ, "res/sprite/item_icons.png")

OSCURA, MEDIA, CLARA = 11, 12, 13
TINTA = {".": 0, "o": OSCURA, "x": MEDIA, "#": CLARA}

ICONOS = [
 ("MAPA", [
  "................",
  ".####......####.",
  ".#xxx#....#xxx#.",
  ".#xxxx####xxxx#.",
  ".#xxxxooooxxxx#.",
  ".#xxxooxxooxxx#.",
  ".#xxxoxxxxoxxx#.",
  ".#xxxoxxxxoxxx#.",
  ".#xxxooxxooxxx#.",
  ".#xxxxoxxoxxxx#.",
  ".#xxxxxooxxxxx#.",
  ".#xxxxxxoxxxxx#.",
  ".#xxxx####xxxx#.",
  ".#xxx#....#xxx#.",
  ".####......####.",
  "................"]),
 ("BRUJULA", [
  ".......##.......",
  "....########....",
  "...###....###...",
  "..##........##..",
  ".##....##....##.",
  ".##....##....##.",
  ".#.....##.....#.",
  "##....####....##",
  "##....xxxx....##",
  ".#.....xx.....#.",
  ".##....xx....##.",
  ".##....xx....##.",
  "..##........##..",
  "...###....###...",
  "....########....",
  ".......##......."]),
]


def indices(d):
    if len(d) != 16 or any(len(f) != 16 for f in d):
        sys.exit("un icono no mide 16x16")
    return [TINTA[c] for f in d for c in f]


def escribir_png(ruta, iconos, colormap):
    n = len(iconos)
    w, h = n * 16, 16
    filas = b""
    for y in range(16):
        filas += b"\x00" + bytes(iconos[t][y * 16 + x] for t in range(n) for x in range(16))
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
    colormap = [(0, 0, 36)] + [(0, 0, 0)] * 15
    colormap[OSCURA] = (73, 0, 73)
    colormap[MEDIA] = (182, 73, 146)
    colormap[CLARA] = (255, 182, 219)
    w, h = escribir_png(SAL, [indices(d) for _, d in ICONOS], colormap)
    print("%s: %dx%d, %d iconos (%s)"
          % (os.path.relpath(SAL, RAIZ), w, h, len(ICONOS),
             ", ".join(n for n, _ in ICONOS)))


main()
