#!/usr/bin/env python3
"""Arma res/sprite/enemy.png (8x8) y res/sprite/enemy16.png (16x16).

Una silueta por especie (enemy.h, mismo orden) y dos fotogramas por especie:
el par es 2*especie el peligroso y 2*especie+1 el vulnerable. La forma es la
senal de estado -- macizo mata, hueco con el nucleo blanco se atropella -- y
el color solo acompana. Los dos tamanos son la misma silueta, plana.

PAL2: 0 transparente, 1 lo pisa el menu (planeta completado), 15 lo pisa
"BACK TO SPACE". Del 2 al 7 la tinta de cada especie y el 8 el blanco del
nucleo.
"""
import os, struct, sys, zlib

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLANCO = 8
CLARA_BASE = 2


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


def plano(f, n, clara):
    """Silueta maciza si mata, contorno hueco con nucleo si se atropella."""
    m = silueta(f, n)
    lx, ly, lado = nucleo(m, n)
    peligro = [[clara if m[y][x] else 0 for x in range(n)] for y in range(n)]
    vulne = [[0] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            if not m[y][x]:
                continue
            if borde(m, n, x, y):
                vulne[y][x] = clara
            elif (lx <= x < lx + lado) and (ly <= y < ly + lado):
                vulne[y][x] = BLANCO
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
        for i, (_nom, _rgb, f) in enumerate(ESPECIES):
            marcos.extend(plano(f, n, CLARA_BASE + i))
        w, h = escribir(os.path.join(RAIZ, "res/sprite", nombre), marcos, n, paleta)
        print("res/sprite/%s: %dx%d, %d fotogramas" % (nombre, w, h, len(marcos)))

    if "-v" in sys.argv:
        for i, (nom, _rgb, f) in enumerate(ESPECIES):
            clara = CLARA_BASE + i
            p, v = plano(f, 16, clara)
            print("\n%s  tinta %d" % (nom, clara))
            for y in range(16):
                a = "".join("#" if c == clara else "." for c in p[y])
                b = "".join("W" if c == BLANCO else "#" if c == clara else "." for c in v[y])
                print("  %s   %s" % (a, b))


main()
