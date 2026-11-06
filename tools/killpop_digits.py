#!/usr/bin/env python3
"""Saca res/sprite/killpop_digits.png de la fuente del HUD.

Son las once celdas que necesita el cartelito de puntos que sube del enemigo
muerto: los diez digitos y el punto del separador de millar. No es arte nuevo:
se RECORTAN de res/sprite/font_hud.png, asi que el numero que sube es
exactamente la misma letra que el marcador de arriba a la derecha -- si un dia
cambia la fuente, se vuelve a correr esto y siguen siendo la misma.

Por que sprites y no texto de fondo: un texto son teselas y solo puede subir
de 8 en 8 px, que se ve a saltos (peticion del usuario: "la animacion de los
puntos de los enemigos tiene que ser mas fluida"). Un sprite se coloca al
pixel.

La hoja es una fila de once celdas de 8x8 y rescomp la trocea en un fotograma
por celda (SPRITE ... 1 1), en este orden: 0..9 y luego el punto.

Uso: tools/killpop_digits.py
"""
import os, struct, sys, zlib

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUENTE = os.path.join(RAIZ, "res/sprite/font_hud.png")
SALIDA = os.path.join(RAIZ, "res/sprite/killpop_digits.png")

CELL = 8
COLS = 16           # la fuente son 16 glifos por fila, desde el ASCII 32
GLIFOS = "0123456789."


def leer_png4(ruta):
    d = open(ruta, "rb").read()
    i, idat, plte, w, h = 8, b"", None, 0, 0
    while i < len(d):
        ln = struct.unpack(">I", d[i:i + 4])[0]
        t, dat = d[i + 4:i + 8], d[i + 8:i + 8 + ln]
        if t == b"IHDR":
            w, h, prof, tipo = struct.unpack(">IIBB", dat[:10])
            if (prof, tipo) != (4, 3):
                sys.exit("%s no es indexado de 4 bits" % ruta)
        elif t == b"PLTE":
            plte = dat
        elif t == b"IDAT":
            idat += dat
        i += 12 + ln
    raw = zlib.decompress(idat)
    paso = (w + 1) // 2
    filas = []
    for y in range(h):
        off = y * (paso + 1)
        if raw[off]:
            sys.exit("%s trae filtros PNG: vuelve a guardarlo sin ellos" % ruta)
        linea = raw[off + 1:off + 1 + paso]
        fila = []
        for b in linea:
            fila.append(b >> 4)
            fila.append(b & 15)
        filas.append(fila[:w])
    return w, h, filas, plte


def trozo(tipo, dat):
    c = tipo + dat
    return struct.pack(">I", len(dat)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)


def main():
    w, h, px, plte = leer_png4(FUENTE)
    if (w, h) != (128, 48):
        sys.exit("%s no mide 128x48" % FUENTE)

    ancho = len(GLIFOS) * CELL
    filas = b""
    for y in range(CELL):
        linea = []
        for ch in GLIFOS:
            g = ord(ch) - 32          # la fuente empieza en el espacio
            cx, cy = (g % COLS) * CELL, (g // COLS) * CELL
            linea += px[cy + y][cx:cx + CELL]
        b = bytearray()
        for i in range(0, len(linea), 2):
            b.append((linea[i] << 4) | linea[i + 1])
        filas += b"\x00" + bytes(b)

    with open(SALIDA, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(trozo(b"IHDR", struct.pack(">IIBBBBB", ancho, CELL, 4, 3, 0, 0, 0)))
        f.write(trozo(b"PLTE", plte))
        f.write(trozo(b"IDAT", zlib.compress(filas, 9)))
        f.write(trozo(b"IEND", b""))

    print("%s: %dx%d, %d celdas (%s)"
          % (os.path.relpath(SALIDA, RAIZ), ancho, CELL, len(GLIFOS), GLIFOS))


main()
