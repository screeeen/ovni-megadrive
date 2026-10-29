#!/usr/bin/env python3
"""Arma res/sprite/player32.png: la nave a 32x32, los 5 fotogramas.

Es la misma nave que res/sprite/player16.png -- platillo redondo, la ranura de
la cabina arriba y las patas colgando -- pero dibujada al tamano que toca en vez
de escalada: a 2x los bordes salen a escalones de 2 px y se le ve el pixel
gordo. Dos colores, como la de 16: el 0 es transparente y el 1 la tinta.

Los fotogramas son los de player.h: 0 la nave normal, 1..4 estrujada contra el
muro que acaba de golpear (en el orden DIR_UP/LEFT/DOWN/RIGHT; estrujarse es
aplastar el cuerpo contra ese lado, no moverlo) y el 5 la del idle, con el
cuerpo hundido sobre las patas. El de 16x16 no tiene generador: su fotograma
del idle lo saca tools/player_tiles16.py del PNG que ya hay.
"""
import os, struct, sys, zlib

from caras import poner

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAL = os.path.join(RAIZ, "res/sprite/player32.png")
TINTA = 1

# Lo que mide cada cosa en tantos por uno del lado del sprite.
CUERPO_ANCHO = 0.97      # diametro del platillo
CUERPO_ALTO  = 0.80
CUERPO_CY    = 0.40      # centro del cuerpo
RANURA_Y     = 0.25      # la muesca de la cabina
RANURA_ALTO  = 0.07
RANURA_MEDIO = 0.065     # media anchura de la muesca
PATA_LARGO   = 0.17      # lo que cuelgan las patas
PATA_X       = 0.40      # salen del canto del platillo, no del centro
APLASTADO    = 0.72      # cuanto se estruja contra el muro
# Que expresion lleva cada fotograma (caras.py), de mayor a menor.
_GOLPE = ("GOLPE", "GOLPE_MINI")
EXPRESION = (("NAVE",), _GOLPE, _GOLPE, _GOLPE, _GOLPE, ("NAVE_IDLE",))
CUERPO_BAJA  = 2         # px que baja el cuerpo en el fotograma del idle


def nave(n, sx, sy, ax, ay, baja=0):
    """El dibujo en un lienzo de n x n. sx/sy aplastan y ax/ay dicen contra
    que lado se apoya (-1 arriba/izquierda, 0 centrado, 1 abajo/derecha).
    baja son los px que se hunde el cuerpo: las patas se acortan otro tanto,
    asi que las puntas no se mueven de su fila y la nave no se despega del
    suelo -- se agacha sobre las patas, que hacen de muelle."""
    m = [[0] * n for _ in range(n)]

    def pinta(x, y):
        if 0 <= x < n and 0 <= y < n:
            m[y][x] = TINTA

    # El lienzo del cuerpo, ya estrujado y apoyado donde toque.
    def mapear(px, py):
        cx = 0.5 + (ax * (1.0 - sx) * 0.5)
        cy = CUERPO_CY + (ay * (1.0 - sy) * 0.5) + (baja / float(n))
        return (cx + (px - 0.5) * sx, cy + (py - CUERPO_CY) * sy)

    for y in range(n):
        for x in range(n):
            px, py = (x + 0.5) / n, (y + 0.5) / n
            cx = 0.5 + (ax * (1.0 - sx) * 0.5)
            cy = CUERPO_CY + (ay * (1.0 - sy) * 0.5) + (baja / float(n))
            u = (px - cx) / (sx * CUERPO_ANCHO * 0.5)
            v = (py - cy) / (sy * CUERPO_ALTO * 0.5)

            if (u * u) + (v * v) <= 1.0:
                pinta(x, y)

    # La ranura de la cabina: una muesca arriba, en el eje.
    for y in range(n):
        for x in range(n):
            px, py = (x + 0.5) / n, (y + 0.5) / n
            cx = 0.5 + (ax * (1.0 - sx) * 0.5)
            cy = CUERPO_CY + (ay * (1.0 - sy) * 0.5) + (baja / float(n))
            ry = cy + (RANURA_Y - CUERPO_CY) * sy
            if abs(px - cx) <= RANURA_MEDIO * sx and abs(py - ry) <= RANURA_ALTO * sy / 2:
                m[y][x] = 0

    # Las patas: dos por lado, colgando del bajo del cuerpo.
    cx = 0.5 + (ax * (1.0 - sx) * 0.5)
    cy = CUERPO_CY + (ay * (1.0 - sy) * 0.5) + (baja / float(n))
    # Cuelgan del canto del platillo -- la fila mas ancha -- y se ven solo
    # por debajo del cuerpo, igual que en la de 16x16.
    largo = max(2, int(PATA_LARGO * sy * n) - baja)
    for lado in (-1, 1):
        x0 = int((cx + lado * (CUERPO_ANCHO * 0.5 - 0.03) * sx) * n)
        y0 = int(cy * n)
        abajo = int((cy + CUERPO_ALTO * 0.5 * sy) * n)

        for y in range(y0, abajo + largo):
            pinta(x0, y)
        pinta(x0 - 1, abajo + largo - 1)
        pinta(x0 + 1, abajo + largo - 1)

    return m


def trozo(tipo, dat):
    c = tipo + dat
    return struct.pack(">I", len(dat)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)


def escribir(ruta, marcos, n, paleta):
    w, h = len(marcos) * n, n
    filas = b""
    for y in range(n):
        filas += b"\x00" + bytes(marcos[t][y][x] for t in range(len(marcos)) for x in range(n))
    with open(ruta, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(trozo(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)))
        f.write(trozo(b"PLTE", b"".join(bytes(c) for c in paleta)))
        f.write(trozo(b"IDAT", zlib.compress(filas, 9)))
        f.write(trozo(b"IEND", b""))
    return w, h


def main():
    n = 32
    A = APLASTADO
    # 0 normal, y 1..4 contra arriba / izquierda / abajo / derecha.
    marcos = [nave(n, 1.0, 1.0, 0, 0),
              nave(n, 1.0, A, 0, -1),
              nave(n, A, 1.0, -1, 0),
              nave(n, 1.0, A, 0, 1),
              nave(n, A, 1.0, 1, 0),
              nave(n, 1.0, 1.0, 0, 0, CUERPO_BAJA)]

    # La cara, en hueco (caras.py): la nave tiene dos colores y el 0 es el
    # transparente, asi que los ojos son negativo. Estrujada pone cara de
    # golpe y parada entorna los ojos, que es lo que le da el caracter.
    for i, m in enumerate(marcos):
        if EXPRESION[i]:
            poner(m, m, n, EXPRESION[i], 0, escala=2, margen=2)
    paleta = [(37, 37, 37), (152, 125, 250)] + [(0, 0, 0)] * 14
    w, h = escribir(SAL, marcos, n, paleta)
    print("%s: %dx%d, %d fotogramas de %d" % (os.path.relpath(SAL, RAIZ), w, h, len(marcos), n))

    if "-v" in sys.argv:
        for i, m in enumerate(marcos):
            print("--- fotograma %d" % i)
            for fila in m:
                print("  " + "".join("#" if c else "." for c in fila))


main()
