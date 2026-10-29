#!/usr/bin/env python3
"""Arma res/sprite/enemy.png (8x8) y res/sprite/enemy16.png (16x16).

Una silueta por especie (enemy.h, mismo orden) y dos estados, y encima la
postura estrujada contra el muro que acaba de golpear, como la nave (peticion
del usuario). Pero NO para todas: solo las de ESTRUJAN, que son las que se
mueven con ease ("solo haz el squeeze animation en aquellos enemigos que se
mueven con ease").

De ahi el orden del fichero, que es el que lee Enemy_frame():
  0..11   los normales, 2*especie + vulnerable, como siempre
  12..35  los estrujados, cuatro (DIR_UP/LEFT/DOWN/RIGHT) por cada estado de
          cada especie que se estruja, en el orden de ESTRUJAN

Estrujarse es lo que hace la nave en tools/player_tiles.py: achatar el cuerpo
en el eje del golpe y apoyarlo contra ese lado, sin mover el sprite. La forma
sigue siendo la senal de estado -- macizo mata, hueco con el nucleo blanco se
atropella -- y el color solo acompana.

Y cada especie lleva su cara, sacada de como se mueve (caras.py): en hueco la
del cuerpo macizo y en blanco la del contorno, para que el blanco siga siendo
solo la senal de "se le puede atropellar". A 8x8 no cabe ninguna, y da igual:
de enemy.png el juego solo usa la paleta.

PAL2: 0 transparente, 1 lo pisa el menu (planeta completado), 15 lo pisa
"BACK TO SPACE". Del 2 al 7 la tinta de cada especie y el 8 el blanco del
nucleo.
"""
import os, struct, sys, zlib

from caras import poner

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLANCO = 8
CLARA_BASE = 2
APLASTADO = 0.72   # cuanto se estruja contra el muro, el mismo que la nave
ESTRUJAN = (3, 4, 5)   # ACECHO, DESLIZA y ESPECTRO: las que llevan ease


def espectro(u, v, n):
    if v > 0.7:                                 # los picos del faldon
        return int((u + 1.0) * 4.0) % 2 == 0    # 4 picos, mida lo que mida
    if v < 0.0:                                 # la cupula
        return (u * u) + (v * v) <= 1.02
    return abs(u) <= 1.0


# (nombre, RGB, silueta). Los niveles son los 0..7 que da el VDP. La silueta
# recibe coordenadas normalizadas en (-1,1) y el tamano en celdas.
ESPECIES = [
    ("PATRULLA", (0, 7, 7), lambda u, v, n: abs(u) + abs(v) <= 1.05),
    ("TORRETA",  (7, 3, 0), lambda u, v, n: abs(u) + abs(v) <= 1.6),
    ("RONDA",    (2, 7, 0), lambda u, v, n: abs(u) <= (v + 1.0) * 0.5 + 0.1),
    ("ACECHO",   (7, 0, 2), lambda u, v, n: (abs(u) <= 0.5) or (abs(v) <= 0.5)),
    ("DESLIZA",  (2, 3, 7), lambda u, v, n: abs(u) <= abs(v) + 0.2),
    ("ESPECTRO", (5, 5, 7), espectro),
]


def aplastar(f, d):
    """La misma silueta achatada contra el muro d (0..3 = arriba, izquierda,
    abajo, derecha) y apoyada en el. Devuelve otra funcion de silueta, asi que
    el nucleo del fotograma vulnerable se recalcula solo sobre la nueva forma."""
    a = APLASTADO

    def estrujada(u, v, n):
        # Deshacer el achatado: de donde del cuerpo original viene este pixel.
        if d == 0:   u0, v0 = u, (v + 1.0) / a - 1.0   # contra arriba
        elif d == 1: u0, v0 = (u + 1.0) / a - 1.0, v   # contra la izquierda
        elif d == 2: u0, v0 = u, (v - 1.0) / a + 1.0   # contra abajo
        else:        u0, v0 = (u - 1.0) / a + 1.0, v   # contra la derecha

        if (abs(u0) > 1.0) or (abs(v0) > 1.0):
            return False   # fuera del cuerpo: ahi ya no hay nave
        return bool(f(u0, v0, n))

    return estrujada


def silueta(f, n):
    m = [[False] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            u = ((x + 0.5) / n) * 2.0 - 1.0
            v = ((y + 0.5) / n) * 2.0 - 1.0
            m[y][x] = bool(f(u, v, n))
    return m


def nucleo(m, n):
    """Esquina del cuadro blanco: en el centro de masa, no en el de la caja.
    En un triangulo o en un fantasma el centro geometrico se va a un borde."""
    celdas = [(x, y) for y in range(n) for x in range(n) if m[y][x]]
    lado = n // 4
    cx = sum(x for x, _ in celdas) // len(celdas)
    cy = sum(y for _, y in celdas) // len(celdas)
    return (min(max(cx - lado // 2 + 1, 1), n - lado - 1),
            min(max(cy - lado // 2 + 1, 1), n - lado - 1), lado)


def borde(m, n, x, y):
    def dentro(a, b):
        return (0 <= a < n) and (0 <= b < n) and m[b][a]
    return not (dentro(x - 1, y) and dentro(x + 1, y) and dentro(x, y - 1) and dentro(x, y + 1))


def plano(f, n, clara, cara=None, caraV=("SUSTO", "SUSTO_MINI")):
    """Silueta maciza si mata, contorno hueco si se atropella, y la cara
    encima: en hueco la del cuerpo macizo, en blanco la del hueco."""
    m = silueta(f, n)
    peligro = [[clara if m[y][x] else 0 for x in range(n)] for y in range(n)]
    vulne = [[clara if (m[y][x] and borde(m, n, x, y)) else 0
              for x in range(n)] for y in range(n)]

    if (cara is None) or (n < 16):
        # A 8x8 no cabe una cara: esos fotogramas se quedan con el cuadro
        # blanco de antes. Da igual, porque no se dibujan nunca -- de
        # enemy.png el juego solo usa la paleta (main.c).
        lx, ly, lado = nucleo(m, n)
        for y in range(ly, ly + lado):
            for x in range(lx, lx + lado):
                if m[y][x] and not borde(m, n, x, y):
                    vulne[y][x] = BLANCO
        return peligro, vulne

    poner(peligro, m, n, cara, 0)
    poner(vulne, m, n, caraV, BLANCO)
    return peligro, vulne


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
    nivel = lambda v: min(255, v * 36)           # 3 bits por canal, lo que da el VDP
    paleta = [(25, 23, 22)] * 16
    paleta[1] = (73, 0, 73)                      # lo pisa el menu
    paleta[BLANCO] = (255, 255, 255)
    for i, (_n, rgb, _f) in enumerate(ESPECIES):
        paleta[CLARA_BASE + i] = tuple(nivel(c) for c in rgb)

    for n, nombre in ((8, "enemy.png"), (16, "enemy16.png")):
        marcos = []
        for i, (nom, _rgb, f) in enumerate(ESPECIES):
            peligro, vulne = plano(f, n, CLARA_BASE + i, nom)
            marcos.extend((peligro, vulne))
        for i in ESTRUJAN:
            nom, _rgb, f = ESPECIES[i]
            # Recien estrellado se le queda cara de aturdido, en los dos
            # estados: el golpe manda sobre el caracter.
            posturas = [plano(aplastar(f, d), n, CLARA_BASE + i, ("GOLPE", "GOLPE_MINI"), ("GOLPE", "GOLPE_MINI"))
                        for d in range(4)]
            for estado in (0, 1):
                marcos.extend(p[estado] for p in posturas)
        w, h = escribir(os.path.join(RAIZ, "res/sprite", nombre), marcos, n, paleta)
        print("res/sprite/%s: %dx%d, %d fotogramas" % (nombre, w, h, len(marcos)))

    if "-v" in sys.argv:
        muros = ("normal", "arriba", "izquierda", "abajo", "derecha")
        for i, (nom, _rgb, f) in enumerate(ESPECIES):
            clara = CLARA_BASE + i
            estruja = i in ESTRUJAN
            print("\n%s  tinta %d   (peligroso, vulnerable)%s"
                  % (nom, clara, "" if estruja else "   -- sin estrujar, va a paso clavado"))
            variantes = [f] + ([aplastar(f, k) for k in range(4)] if estruja else [])
            for d, g in enumerate(variantes):
                p, v = (plano(g, 16, clara, nom) if d == 0
                        else plano(g, 16, clara, ("GOLPE", "GOLPE_MINI"), ("GOLPE", "GOLPE_MINI")))
                print("  -- %s" % muros[d])
                for y in range(16):
                    a = "".join("#" if c == clara else "." for c in p[y])
                    b = "".join("W" if c == BLANCO else "#" if c == clara else "." for c in v[y])
                    print("     %s   %s" % (a, b))


main()
