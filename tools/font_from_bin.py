#!/usr/bin/env python3
"""Convierte una fuente bitmap de 8x8 en binario crudo a res/sprite/font.png.

El formato de entrada son 96 caracteres de 8 bytes (768 bytes en total),
un bit por pixel, empezando en el ASCII 32 (espacio). Es el formato de la
ROM del ZX Spectrum, y el que traen los ZIP de ZX Origins como
"Sinclair ZX Spectrum" -- de ahi que no haga falta rasterizar nada.

    python3 tools/font_from_bin.py FUENTE.bin [salida.png]

Sin segundo argumento escribe directamente res/sprite/font.png.

El PNG sale indexado de 4 bits con 16 entradas y la TINTA EN EL INDICE 15,
igual que la font_default.png de SGDK. Eso no es cosmetico: rescomp guarda
indices de paleta, no colores, asi que el indice ES el color que se vera.
El 15 es el que todo este juego trata como tinta de texto (menu.c y
RETURN_MSG_INK_INDEX leen (PAL * 16) + 15) y el unico que nadie reescribe
en PAL0 -- maze.c solo toca el 0, el 1 y el 5. Generar la fuente con la
tinta en el indice 1, como hacia la version anterior de este script, la
pintaba del color de los muros del laberinto.
"""
import struct
import sys
import zlib

COLS, ROWS, CELL = 16, 6, 8
WIDTH, HEIGHT = COLS * CELL, ROWS * CELL
INK_INDEX = 15


def chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path: str, pixels: list) -> None:
    """pixels[y][x] -> 0 (fondo) o INK_INDEX (tinta). PNG indexado 4bpp."""
    palette = bytearray(16 * 3)
    palette[INK_INDEX * 3:INK_INDEX * 3 + 3] = b"\xff\xff\xff"

    raw = bytearray()
    for row in pixels:
        raw.append(0)                               # filtro: ninguno
        for x in range(0, WIDTH, 2):                # 2 pixeles por byte
            raw.append((row[x] << 4) | row[x + 1])

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 4, 3, 0, 0, 0))
           + chunk(b"PLTE", bytes(palette))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else "res/sprite/font.png"

    with open(src, "rb") as f:
        data = f.read()

    want = COLS * ROWS * CELL
    if len(data) < want:
        print(f"error: {src} tiene {len(data)} bytes, hacen falta {want}")
        return 1
    if len(data) > want:
        print(f"aviso: {src} tiene {len(data)} bytes, uso los primeros {want}")
        data = data[:want]

    pixels = [[0] * WIDTH for _ in range(HEIGHT)]
    ink = 0
    for glyph in range(COLS * ROWS):
        gx, gy = (glyph % COLS) * CELL, (glyph // COLS) * CELL
        for row in range(CELL):
            bits = data[glyph * CELL + row]
            for col in range(CELL):
                if bits & (0x80 >> col):            # bit alto = pixel izquierdo
                    pixels[gy + row][gx + col] = INK_INDEX
                    ink += 1

    write_png(dst, pixels)
    print(f"escrito {dst}  ({WIDTH}x{HEIGHT}, 4bpp indexado, tinta en el indice {INK_INDEX}, {ink} pixeles)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
