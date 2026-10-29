#!/usr/bin/env python3
"""Arma res/sprite/player16.png a partir de res/sprite/player16_src.png: los
cinco fotogramas de siempre mas el 5, el del idle, y a todos la cara.

La nave de 16x16 no tiene generador -- es arte que ya estaba -- asi que esto
trabaja sobre pixeles y no sobre formulas, al contrario que su hermana de
32x32 (tools/player_tiles.py, que dibuja los seis con la cara puesta):

  - el fotograma del idle sale del 0 bajando todo el dibujo dy px y dejando la
    ultima fila, las puntas de las patas, clavada donde estaba. Lo que cae
    sobre esa fila se pierde, que es justo el tramo de pata que se come el
    muelle: asi el cuerpo se mueve y la nave no se despega del suelo.
  - la cara va en hueco (caras.py): la nave tiene dos colores y el 0 es el
    transparente, asi que los ojos son negativo. Estrujada pone cara de golpe
    y parada entorna los ojos.

El PNG fuente es el que manda y no se toca nunca -- estampar una cara sobre
una cara ya estampada no daria lo mismo, asi que la salida no puede ser
tambien la entrada.
"""
import os, struct, sys, zlib

from caras import poner

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUENTE = os.path.join(RAIZ, "res/sprite/player16_src.png")
SAL = os.path.join(RAIZ, "res/sprite/player16.png")
NORMALES = 5             # los que trae la fuente: 0 la nave y 1..4 estrujada
CUERPO_BAJA = 1          # px que baja el cuerpo
# Que expresion lleva cada fotograma (caras.py), de mayor a menor: estrujada
# de lado el cuerpo no da para la grande y poner() se queda con la mini.
# None deja el fotograma sin cara, por si los 9 px de la estrujada de lado
# resultan demasiado poco sitio para una.
GOLPE = ("GOLPE", "GOLPE_MINI")
EXPRESION = (("NAVE",), GOLPE, GOLPE, GOLPE, GOLPE, ("NAVE_IDLE",))


def leer(ruta):
    dat, i, idat, pal = open(ruta, "rb").read(), 8, b"", None
    while i < len(dat):
        n = struct.unpack(">I", dat[i:i + 4])[0]
        t, cuerpo = dat[i + 4:i + 8], dat[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h, prof, tipo = struct.unpack(">IIBB", cuerpo[:10])[0:4]
            if (prof, tipo) != (8, 3):
                sys.exit("%s no es paleta de 8 bits" % ruta)
        elif t == b"PLTE":
            pal = [tuple(cuerpo[k:k + 3]) for k in range(0, len(cuerpo), 3)]
        elif t == b"IDAT":
            idat += cuerpo
        i += 12 + n

    crudo, filas, previa, p = zlib.decompress(idat), [], bytearray(w), 0
    for _ in range(h):
        f, linea = crudo[p], bytearray(crudo[p + 1:p + 1 + w])
        p += 1 + w
        if f == 1:
            for x in range(1, w):
                linea[x] = (linea[x] + linea[x - 1]) & 0xFF
        elif f == 2:
            for x in range(w):
                linea[x] = (linea[x] + previa[x]) & 0xFF
        elif f == 3:
            for x in range(w):
                linea[x] = (linea[x] + ((linea[x - 1] if x else 0) + previa[x]) // 2) & 0xFF
        elif f != 0:
            sys.exit("filtro PNG %d sin implementar" % f)
        filas.append(list(linea))
        previa = linea
    return w, h, filas, pal


def idle(marco, n, dy):
    """El marco con el cuerpo dy px mas abajo y las puntas en su fila."""
    salida = [[0] * n for _ in range(n)]

    for y in range(n - 1 - dy):
        for x in range(n):
            salida[y + dy][x] = marco[y][x]
    for x in range(n):
        salida[n - 1][x] = marco[n - 1][x]
    return salida


def trozo(tipo, dat):
    c = tipo + dat
    return struct.pack(">I", len(dat)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)


def main():
    w, n, filas, pal = leer(FUENTE)
    if w < (NORMALES * n):
        sys.exit("%s tiene menos de %d fotogramas" % (FUENTE, NORMALES))

    marcos = [[[filas[y][(t * n) + x] for x in range(n)] for y in range(n)]
              for t in range(NORMALES)]
    marcos.append(idle(marcos[0], n, CUERPO_BAJA))
    puestas = [poner(m, m, n, EXPRESION[i], 0)[0] if EXPRESION[i] else "sin cara"
               for i, m in enumerate(marcos)]
    nuevo = marcos[5]

    ancho = len(marcos) * n
    datos = b""
    for y in range(n):
        datos += b"\x00" + bytes(marcos[t][y][x] for t in range(len(marcos)) for x in range(n))
    with open(SAL, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(trozo(b"IHDR", struct.pack(">IIBBBBB", ancho, n, 8, 3, 0, 0, 0)))
        f.write(trozo(b"PLTE", b"".join(bytes(c) for c in pal)))
        f.write(trozo(b"IDAT", zlib.compress(datos, 9)))
        f.write(trozo(b"IEND", b""))
    print("%s: %dx%d, %d fotogramas de %d (el cuerpo baja %d px)"
          % (os.path.relpath(SAL, RAIZ), ancho, n, len(marcos), n, CUERPO_BAJA))

    if "-v" in sys.argv:
        for i, m in enumerate(marcos):
            print("--- fotograma %d, cara %s" % (i, puestas[i]))
            for y in range(n):
                print("  " + "".join("#" if m[y][x] else "." for x in range(n)))


main()
