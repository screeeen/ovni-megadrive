#!/usr/bin/env python3
"""Arma inc/plant_tiles_gen.h: la tabla de siluetas de las plantas.

Cada planta del juego tiene un INDICE unico (ver plants.h) y de ese indice sale
su forma, sin repetirse ninguna: indice -> (perfil, relleno, tallo) en base
mixta, que es inyectivo, y aqui se comprueba ademas que las PLANT_SHAPE_COUNT
combinaciones dan PLANT_SHAPE_COUNT dibujos distintos.

La planta se dibuja en C (Plants_buildTile); esto solo elige y verifica las
siluetas. Si se cambia una regla hay que cambiarla en los dos sitios. La de
16x16 que se ve en la sala es ese mismo dibujo a 2x, y tambien se hace en C
(buildTile16), asi que aqui no hay nada de ella.
"""
import os, random, struct, subprocess, sys, zlib

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(RAIZ, "inc/plant_tiles_gen.h")

# Coprimos entre si a proposito: la forma sale de (idx%PERFILES, idx%RELLENOS,
# idx%TALLOS), que por el teorema chino del resto es inyectivo mientras el
# indice no llegue al producto. Asi cada planta cambia las tres cosas a la vez
# en vez de ir girando solo la ultima.
PERFILES = 127
RELLENOS = 6
TALLOS   = 5

SUELO, OSCURA, MEDIA, CLARA = 2, 11, 12, 13
CROWN_ROWS = 5          # filas 0..4; el tallo va de la base de la copa a la 6
CENTRO = 3              # columna de simetria; la 7 se deja libre siempre
RAICES = {0: (2, 3, 4), 1: (1, 2, 3, 4, 5), 2: (2, 3, 4), 3: (1, 2, 4, 5),
          4: (2, 3, 4)}


def copa_celdas(h):
    """Las celdas de la copa de un perfil: por fila, un ancho impar centrado."""
    cs = set()
    for r in range(CROWN_ROWS):
        if h[r] == 0:
            continue
        w = h[r] - 1
        for x in range(CENTRO - w, CENTRO + w + 1):
            cs.add((x, r))
    return cs


def perfil_valido(h):
    llenas = [r for r in range(CROWN_ROWS) if h[r]]
    if not llenas:
        return False
    if llenas != list(range(llenas[0], llenas[-1] + 1)):
        return False                                   # un solo bloque, sin huecos
    for a, b in zip(llenas, llenas[1:]):
        if abs(h[a] - h[b]) > 2:
            return False                               # sin escalones salvajes
    cs = copa_celdas(h)
    if len(cs) < 8 or max(h) < 2:
        return False                                   # ni minuscula ni un palo
    return bool(interiores(cs))                        # con interior: si no, los
                                                       # rellenos no se distinguen


def interiores(cs):
    return {(x, y) for (x, y) in cs
            if all((x + dx, y + dy) in cs for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))}


def dibujar(h, relleno, tallo):
    """El mismo dibujo que hace Plants_buildTile en C. 8x8 de indices de PAL0."""
    px = [SUELO] * 64
    cs = copa_celdas(h)
    dentro = interiores(cs)
    base = max(y for (_, y) in cs)

    for (x, y) in cs:
        if relleno == 0:
            c = CLARA
        elif relleno == 1:
            c = MEDIA if y == base else CLARA
        elif relleno == 2:
            c = MEDIA if (x, y) in dentro else CLARA
        elif relleno == 3:
            c = None if (x, y) in dentro else CLARA
        elif relleno == 4:
            c = CLARA if (x + y) & 1 else MEDIA
        else:
            c = CLARA if (x, y) not in dentro else (MEDIA if (x + y) & 1 else None)
        if c is not None:
            px[y * 8 + x] = c

    cols = {0: (CENTRO,), 1: (CENTRO - 1, CENTRO, CENTRO + 1), 2: (CENTRO,),
            3: (CENTRO - 1, CENTRO + 1), 4: (CENTRO,)}[tallo]
    for y in range(base + 1, 7):
        for x in cols:
            px[y * 8 + x] = MEDIA
    if tallo == 2:
        for x in (CENTRO - 2, CENTRO - 1, CENTRO + 1, CENTRO + 2):
            px[min(6, base + 2) * 8 + x] = MEDIA
    elif tallo == 4:
        for x in (CENTRO + 1, CENTRO + 2):
            px[(base + 1) * 8 + x] = MEDIA

    raiz = RAICES[tallo]
    for x in raiz:
        px[7 * 8 + x] = OSCURA
    return px


def elegir_perfiles():
    """Los PERFILES que caben, cada uno con sus RELLENOS x TALLOS dibujos nuevos."""
    todos = []
    for n in range(5 ** CROWN_ROWS):
        h, v = [], n
        for _ in range(CROWN_ROWS):
            h.append(v % 5); v //= 5
        if perfil_valido(h):
            todos.append(tuple(h))
    random.Random(20261019).shuffle(todos)   # si no, los vecinos salen calcados

    vistos, elegidos = {}, []
    for h in todos:
        lote = [bytes(dibujar(h, r, t)) for r in range(RELLENOS) for t in range(TALLOS)]
        if len(set(lote)) != len(lote) or any(d in vistos for d in lote):
            continue
        for d in lote:
            vistos[d] = h
        elegidos.append(h)
        if len(elegidos) == PERFILES:
            break
    if len(elegidos) < PERFILES:
        sys.exit("solo salen %d perfiles de %d" % (len(elegidos), PERFILES))
    return elegidos, len(vistos)


def escribir_h(perfiles):
    total = PERFILES * RELLENOS * TALLOS
    with open(GEN, "w") as f:
        f.write("// GENERADO POR tools/plant_tiles.py -- no editar a mano.\n"
                "// Solo lo incluye plants.c.\n"
                "#ifndef _PLANT_TILES_GEN_H_\n#define _PLANT_TILES_GEN_H_\n\n")
        f.write("#define PLANT_PROFILE_COUNT %d\n" % PERFILES)
        f.write("#define PLANT_FILL_COUNT    %d\n" % RELLENOS)
        f.write("#define PLANT_STEM_COUNT    %d\n" % TALLOS)
        f.write("#define PLANT_SHAPE_COUNT   %d   // todas distintas, comprobado al generar\n\n" % total)
        f.write("#define PLANT_CROWN_ROWS    %d\n" % CROWN_ROWS)
        f.write("#define PLANT_CROWN_MID     %d\n\n" % CENTRO)
        f.write("// Las columnas de la raiz de cada tallo, una mascara por tallo.\n")
        f.write("static const u8 plantRoot[PLANT_STEM_COUNT] = { %s };\n\n"
                % ", ".join("0x%02X" % sum(1 << x for x in RAICES[t]) for t in range(TALLOS)))
        f.write("// Ancho de cada fila de la copa, de arriba abajo: 0 vacia, y si no\n"
                "// 2h-1 celdas centradas en la columna 3.\n")
        f.write("static const u8 plantProfile[PLANT_PROFILE_COUNT][PLANT_CROWN_ROWS] = {\n")
        for h in perfiles:
            f.write("    { %s },\n" % ", ".join(str(v) for v in h))
        f.write("};\n\n#endif // _PLANT_TILES_GEN_H_\n")


def main():
    perfiles, distintos = elegir_perfiles()
    escribir_h(perfiles)

    print("%d perfiles x %d rellenos x %d tallos = %d formas, %d dibujos distintos"
          % (PERFILES, RELLENOS, TALLOS, PERFILES * RELLENOS * TALLOS, distintos))
    assert distintos == PERFILES * RELLENOS * TALLOS


main()
