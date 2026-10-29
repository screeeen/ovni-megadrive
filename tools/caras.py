#!/usr/bin/env python3
"""Las caras: plantillas y el encaje, compartidos por los tres generadores de
arte (enemy_tiles.py, player_tiles.py, player_tiles16.py).

Peticion del usuario: "ponle caras a los enemigos y al player con expresiones.
Tienen que tener personalidad". La 'o' es donde cae la marca y el '.' donde no
se toca nada. Estan dibujadas a la escala del sprite de 16x16, que es el que
se juega; para el de 32x32 valen las mismas al doble (doble()).

La marca se pone en HUECO -- un agujero en el cuerpo macizo -- y no en claro.
En los enemigos eso es obligado: el blanco es la senal de "se le puede
atropellar" y no puede aparecer en el que mata. En el fotograma vulnerable, que
es un contorno hueco, la cara va justo al contrario, en blanco, y hace ella de
bulto.
"""

CARAS = {
    # --- los enemigos, uno por especie segun como se mueve (enemy.h)
    # va y viene por su carril sin preguntarse nada: cara de pan
    "PATRULLA": ("oo..oo",
                 "oo..oo",
                 "......",
                 ".oooo."),
    # el centinela que no se mueve de su sitio: cejas caidas y morro apretado
    "TORRETA":  ("oo......oo",
                 ".oo....oo.",
                 "..oo..oo..",
                 "..........",
                 "...oooo..."),
    # da la vuelta al perimetro un dia y otro: chulesca, guina un ojo
    "RONDA":    ("oo..oo",
                 "oo....",
                 "......",
                 "..ooo."),
    # te mira fijo y avanza en cuanto te paras: ojos enormes y sin boca
    "ACECHO":   ("oo..oo",
                 "oo..oo",
                 "oo..oo"),
    # se lanza de muro a muro: ojos entrecerrados y la boca abierta gritando
    "DESLIZA":  ("oo..oo",
                 "......",
                 ".oooo.",
                 ".oooo."),
    # atraviesa paredes flotando: ojos desparejos y la boca ondulada
    "ESPECTRO": ("oo...o",
                 "oo....",
                 "......",
                 "o.o.o."),

    # --- los estados, que mandan sobre el caracter
    # cuando se le puede atropellar: la cara de susto ES la senal de estado,
    # asi que es la misma para todas
    "SUSTO":    ("oo..oo",
                 "oo..oo",
                 "......",
                 "oooooo"),
    # recien estrellado contra el muro: ojos apretados y la boca torcida
    "GOLPE":    ("oo..oo",
                 "......",
                 "o.oo.o"),

    # --- las mismas en pequeno, para cuerpos que no dan para mas: la nave
    # estrujada contra un muro de lado se queda en 9 px de ancho y la cara
    # grande la convierte en un queso. poner() elige sola.
    "SUSTO_MINI": ("o..o",
                   "o..o",
                   "....",
                   "oooo"),
    "GOLPE_MINI": ("o..o",
                   "....",
                   ".oo."),

    # --- la nave
    # el protagonista: ojos abiertos y la boca pequena, atenta
    "NAVE":     ("oo..oo",
                 "oo..oo",
                 "......",
                 ".oooo."),
    # parada y flotando: ojos entornados y media sonrisa
    "NAVE_IDLE": ("oo..oo",
                  "......",
                  "o....o",
                  ".oooo."),
}


def doble(cara):
    """La misma plantilla al doble, para el sprite de 32x32."""
    return tuple("".join(c * 2 for c in fila) for fila in cara for _ in (0, 1))


def interior(m, n, margen=1):
    """Donde se puede poner la cara: cuerpo con cuerpo alrededor. Pedir ese
    margen es lo que hace que un ojo lea a ojo y no a mordisco en la silueta
    -- sin el, el sitio "que cabe" acaba pegado al contorno y la forma se
    deshace."""
    def lleno(x, y):
        return (0 <= x < n) and (0 <= y < n) and m[y][x]

    r = range(-margen, margen + 1)
    return [[all(lleno(x + dx, y + dy) for dx in r for dy in r)
             for x in range(n)] for y in range(n)]


def encajar(dentro, n, cara, cx, cy):
    """Donde cae la cara: la posicion que mete mas marcas en el cuerpo, y de
    las empatadas la mas centrada. Cada cuerpo tiene su forma, asi que el
    sitio no se puede fijar a mano -- el reloj de arena del DESLIZA no tiene
    cuerpo en el centro y la cara se le va al cono de arriba."""
    ah, aw = len(cara), max(len(f) for f in cara)
    marcas = [(dx, dy) for dy, f in enumerate(cara)
              for dx, c in enumerate(f) if c == "o"]
    mejor = None

    for y0 in range(n - ah + 1):
        for x0 in range(n - aw + 1):
            cabe = sum(1 for dx, dy in marcas if dentro[y0 + dy][x0 + dx])
            lejos = abs(x0 + (aw - 1) / 2.0 - cx) + abs(y0 + (ah - 1) / 2.0 - cy)
            if (mejor is None) or ((cabe, -lejos) > (mejor[0], -mejor[1])):
                mejor = (cabe, lejos, x0, y0)

    return mejor[2], mejor[3], mejor[0], len(marcas)


def centro(m, n):
    celdas = [(x, y) for y in range(n) for x in range(n) if m[y][x]]
    return (sum(x for x, _ in celdas) / float(len(celdas)),
            sum(y for _, y in celdas) / float(len(celdas)))


def estampar(capa, dentro, n, cara, x0, y0, tinta):
    """La cara encima, solo donde hay cuerpo con margen."""
    for dy, fila in enumerate(cara):
        for dx, c in enumerate(fila):
            if (c == "o") and dentro[y0 + dy][x0 + dx]:
                capa[y0 + dy][x0 + dx] = tinta


def poner(capa, m, n, nombres, tinta, escala=1, margen=1):
    """Busca el sitio y estampa, de una vez: `capa` es el plano de salida y
    `m` la silueta booleana de la que se saca donde hay cuerpo.

    `nombres` puede ser una lista de candidatos de mayor a menor: se queda con
    el primero que entre entero, y si ninguno entra, con el ultimo. Asi un
    cuerpo estrecho no se lleva una cara que no le cabe."""
    if isinstance(nombres, str):
        nombres = (nombres,)

    dentro = interior(m, n, margen)
    cx, cy = centro(m, n)
    elegida = None

    for nombre in nombres:
        cara = doble(CARAS[nombre]) if escala == 2 else CARAS[nombre]
        x0, y0, cabe, total = encajar(dentro, n, cara, cx, cy)
        elegida = (nombre, cara, x0, y0, cabe, total)
        if cabe == total:
            break

    nombre, cara, x0, y0, cabe, total = elegida
    estampar(capa, dentro, n, cara, x0, y0, tinta)
    return nombre, cabe, total
