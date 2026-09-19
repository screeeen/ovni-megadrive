# Spec: Mapa Guía + Habitaciones (Metroidvania-lite)

Estado: lista para implementación. Extiende el bootstrap actual
(`maze.c`/`player.c`/`main.c`, una única habitación de 20x14 celdas)
a una estructura de múltiples habitaciones conectadas por un Mapa
Guía explorable, con un punto de entrada y otro de salida del mapa.

## 0. Decisiones confirmadas

- **La nave es el jugador**: no hay una sala especial distinta al
  jugador; "nave" es solo cómo llamamos al jugador/su nave.
- **Rejilla del Mapa Guía**: `MAP_COLS = 8`, `MAP_ROWS = 6` por
  defecto (48 celdas), como par de constantes configurables.
- **Persistencia de layout**: el maze interior de cada Habitación es
  el mismo cada vez que se re-entra, vía generación determinista con
  semilla (`roomSeed = hash(mapSeed, col, row)`), no por
  almacenamiento.
- **Algoritmo de generación del Mapa Guía: Prim aleatorio**
  (frontier-based), no DFS-backtracker. El DFS-backtracker genera
  ramas largas y sinuosas que pueden crecer de forma muy desigual
  hacia un lado; Prim aleatorio expande como una mancha desde el
  centro, de forma mucho más equilibrada en las cuatro direcciones
  cardinales.
- **Habitaciones con múltiples puertas**: cada habitación puede
  tener 1-4 puertas activas (una por lado con arista en el árbol). El
  maze interior debe garantizar camino entre **todas** las puertas
  activas de esa habitación, no solo entre una entrada y una salida
  fija — esto sustituye el sesgo original arriba-izquierda →
  abajo-derecha de `maze.c` (que sigue siendo el diseño correcto y
  fiel al original **solo** para el modo de una única habitación;
  aquí se generaliza porque el sistema de múltiples habitaciones no
  tiene equivalente en el juego original).
- **Habitación de Inicio colocada al azar**: `(startCol, startRow)`
  se elige aleatoriamente dentro de la rejilla al generar el mapa
  (`startCol = random() % MAP_COLS`, `startRow = random() %
  MAP_ROWS`), no en el centro fijo. El algoritmo de Prim (§4.1)
  funciona igual de bien desde cualquier semilla — solo cambia la
  forma final del árbol, no su corrección.
- **Entrada y salida del mapa completo**: existe una Habitación de
  Inicio y una Habitación de Salida (meta), distintas, con una
  distancia mínima garantizada entre ellas:
  - Distancia = **saltos por el árbol de puertas** (número de
    habitaciones a cruzar), no distancia en línea recta sobre la
    rejilla.
  - Umbral = **proporcional al tamaño del árbol generado** (≥65% de
    la excentricidad de la Habitación de Inicio), no un número fijo
    de saltos — se adapta solo si cambia `MAP_COLS`/`MAP_ROWS`.
  - La conectividad entre ambas está garantizada gratis: al ser
    parte del mismo árbol de expansión, siempre existe un camino.
- **Loops/ciclos entre habitaciones**: fuera de alcance por ahora.
  Árbol puro (sin ciclos) — las habitaciones con más de una puerta ya
  permiten entrar y salir por lados distintos sin necesitar ciclos.

## 1. Resumen

El juego pasa de "una habitación" a un **Mapa Guía**: una rejilla de
`MAP_COLS x MAP_ROWS` celdas, donde cada celda es o bien una
Habitación jugable (una pantalla completa, como la actual `maze.c`)
o una celda vacía (no navegable, no dibujada). El jugador arranca en
la Habitación de Inicio y debe llegar a la Habitación de Salida,
explorando habitación a habitación por sus puertas. El botón C abre
una vista del Mapa Guía al estilo Impossible Mission (C64) /
Castlevania: SotN, mostrando solo las habitaciones ya descubiertas.

## 2. Terminología

- **Mapa Guía**: la rejilla completa de `MAP_COLS x MAP_ROWS`
  celdas. Generada una vez por partida a partir de un `mapSeed`.
- **Celda**: una posición `(col, row)`. Puede ser `CELL_ROOM` o
  `CELL_EMPTY`.
- **Habitación**: contenido jugable de una celda `CELL_ROOM` — un
  maze de `MAZE_W x MAZE_H`, con 1-4 puertas (N/E/S/O) hacia celdas
  `CELL_ROOM` vecinas.
- **Habitación de Inicio**: celda `(startCol, startRow)` donde
  arranca el jugador. Raíz del árbol de expansión (§4).
- **Habitación de Salida / meta**: celda `(goalCol, goalRow)`,
  elegida tras generar el árbol completo, a distancia mínima
  garantizada de la Habitación de Inicio (§4.3). Sigue existiendo
  como concepto (garantiza que hay un punto "lejos de verdad" del
  inicio), pero **ya no dispara ninguna condición de victoria** —
  ver nota en §7, cambiado tras feedback del usuario: entrar ahí es
  como entrar en cualquier otra habitación, sin congelar el juego.
- **Puerta**: apertura de **al menos 2 celdas de maze de ancho**
  (2×16px) en el borde de una Habitación, conectando con la vecina.

## 3. Estructura de datos

```c
#define MAP_COLS 8
#define MAP_ROWS 6

typedef enum { CELL_EMPTY, CELL_ROOM } CellType;

typedef struct {
    u8 type   : 1;   // CellType
    u8 doorN  : 1;
    u8 doorE  : 1;
    u8 doorS  : 1;
    u8 doorW  : 1;
    u8 visited: 1;
    // 2 bits libres
} MapCell;   // 1 byte por celda

MapCell guideMap[MAP_ROWS][MAP_COLS];   // MAP_ROWS*MAP_COLS bytes
u8 currentCol, currentRow;
u8 startCol, startRow;
u8 goalCol, goalRow;
u16 mapSeed;
```

El maze interior de la Habitación actual sigue viviendo en
`grid[MAZE_H][MAZE_W]` (280 bytes) — solo la habitación donde está
el jugador se mantiene en memoria.

## 4. Generación procedural del Mapa Guía

### 4.1 Semilla y expansión (Prim aleatorio)

```c
typedef struct { u8 col, row; } Coord;

// Frontera: hasta 4 candidatos por celda ya generada. Buffer transitorio,
// solo vive durante la generación (no persiste en RAM tras terminar).
Coord frontier[MAP_COLS * MAP_ROWS * 4];
u16 frontierCount = 0;

static void pushFrontier(s16 col, s16 row)
{
    if (inBounds(col, row) && guideMap[row][col].type == CELL_EMPTY)
        frontier[frontierCount++] = (Coord){ (u8) col, (u8) row };
}

static bool isRoom(s16 col, s16 row)
{
    return inBounds(col, row) && guideMap[row][col].type == CELL_ROOM;
}

void GuideMap_generate(void)
{
    // Posición aleatoria dentro de la rejilla, no un centro fijo.
    startCol = random() % MAP_COLS;
    startRow = random() % MAP_ROWS;

    guideMap[startRow][startCol].type = CELL_ROOM;
    pushFrontier(startCol - 1, startRow);
    pushFrontier(startCol + 1, startRow);
    pushFrontier(startCol, startRow - 1);
    pushFrontier(startCol, startRow + 1);

    while (frontierCount > 0)
    {
        const u16 pick = random() % frontierCount;
        const Coord c = frontier[pick];

        frontier[pick] = frontier[--frontierCount];   // swap-remove

        if (guideMap[c.row][c.col].type != CELL_EMPTY)
            continue;   // ya reclamada por otra rama mientras esperaba en la frontera

        // Vecinos ya-ROOM disponibles: elegir uno al azar para abrir la puerta
        u8 dirs[4];
        u8 n = 0;

        if (isRoom(c.col,     c.row - 1)) dirs[n++] = DIR_N;
        if (isRoom(c.col + 1, c.row))     dirs[n++] = DIR_E;
        if (isRoom(c.col,     c.row + 1)) dirs[n++] = DIR_S;
        if (isRoom(c.col - 1, c.row))     dirs[n++] = DIR_W;

        const u8 dir = dirs[random() % n];

        guideMap[c.row][c.col].type = CELL_ROOM;
        openDoor(c.col, c.row, dir);
        openDoor(neighborCol(c.col, dir), neighborRow(c.row, dir), opposite(dir));

        pushFrontier(c.col - 1, c.row);
        pushFrontier(c.col + 1, c.row);
        pushFrontier(c.col, c.row - 1);
        pushFrontier(c.col, c.row + 1);
    }
}
```

Garantiza por construcción que el grafo de habitaciones es
**conexo** (árbol de expansión) y crece en las cuatro direcciones
desde `(startCol, startRow)` en vez de serpentear hacia una esquina.

### 4.2 Poda opcional

Tras el carve, algunas hojas del árbol pueden revertirse a
`CELL_EMPTY` con probabilidad `PRUNE_CHANCE`, para que no todas las
celdas sean habitación. Solo se pueden podar celdas con exactamente
1 puerta (hojas) — podar un nodo interno rompería la conectividad de
sus descendientes. Ejecutar la selección de entrada/salida (§4.3)
**después** de la poda, sobre el árbol ya final.

### 4.3 Selección de Habitación de Inicio y de Salida

`startCol/startRow` ya es conocido (es la semilla de §4.1). Elegir
`goalCol/goalRow` con BFS sobre las puertas activas:

```c
u8   dist[MAP_ROWS][MAP_COLS];      // 0xFF = no visitado
Coord queue[MAP_COLS * MAP_ROWS];   // reutiliza el buffer de frontier
u16  qHead = 0, qTail = 0;
u8   maxDist = 0;

memset(dist, 0xFF, sizeof(dist));
dist[startRow][startCol] = 0;
queue[qTail++] = (Coord){ startCol, startRow };

while (qHead < qTail)
{
    const Coord cur = queue[qHead++];
    const u8 d = dist[cur.row][cur.col];

    if (d > maxDist) maxDist = d;

    // Por cada puerta activa de cur, visitar la celda vecina si no tiene distancia asignada
    FOR_EACH_ACTIVE_DOOR(cur, neighbor)
    {
        if (dist[neighbor.row][neighbor.col] == 0xFF)
        {
            dist[neighbor.row][neighbor.col] = d + 1;
            queue[qTail++] = neighbor;
        }
    }
}

// Umbral proporcional: al menos 65% de la excentricidad de la Habitación de Inicio
const u8 minGoalDist = (maxDist * 65) / 100;

// Recolectar candidatas (dist >= minGoalDist) y elegir una al azar entre ellas
Coord candidates[MAP_COLS * MAP_ROWS];
u16   candidateCount = 0;

for (each room cell (col,row))
    if (dist[row][col] >= minGoalDist)
        candidates[candidateCount++] = (Coord){ col, row };

const Coord goal = candidates[random() % candidateCount];
goalCol = goal.col;
goalRow = goal.row;
```

`maxDist` nunca es 0 salvo un mapa de una sola habitación (caso
degenerado a evitar con un `MAP_COLS`/`MAP_ROWS` razonable), así que
siempre hay al menos una candidata (el propio nodo más lejano
encontrado). La distancia real entre inicio y salida queda entre el
65% y el 100% de la excentricidad del inicio — "lejos de verdad" sin
ser siempre el mismo par de habitaciones en cada partida.

## 5. Generación procedural de cada Habitación

Reutiliza `Maze_generate()` (recursive-backtracker de
`generateMap.js`), generalizado para un número variable de puertas:

- **Semilla determinista por habitación**:
  `roomSeed = hash(mapSeed, col, row)`, sembrando un PRNG local antes
  de generar (no el `random()` global de SGDK, que avanza en cada
  llamada y daría un maze distinto cada visita). Misma celda → mismo
  maze, siempre:

  ```c
  static u32 roomSeed(u16 mapSeed, u8 col, u8 row)
  {
      u32 h = mapSeed;
      h = h * 374761393u + col;
      h = h * 668265263u + row;
      h ^= h >> 15;
      return h;
  }
  ```

- **Semilla de carve fija en el centro**: en vez del `startX/startY`
  hardcodeado del original (que asumía una única entrada
  arriba-izquierda), el `carve()` de cada habitación arranca siempre
  en `(MAZE_W/2, MAZE_H/2)`. Esto es independiente de por qué puerta
  entra el jugador — el maze interior no cambia según la dirección de
  entrada, solo según `(col,row)` (persistencia, §0).
- **Puertas forzadas para cada lado activo**: por cada
  `doorN/E/S/W == TRUE` en `guideMap[row][col]`, forzar una apertura
  de 2 celdas centrada en ese borde, igual que el punch-through que
  ya existe para `endX/endY` en `maze.c:116-126`, pero repetido por
  cada puerta activa en vez de una sola vez:

  ```c
  if (door[EAST])
  {
      const s16 doorRow = MAZE_H / 2;

      grid[doorRow][MAZE_W - 1]     = PATH;
      grid[doorRow][MAZE_W - 2]     = PATH;
      grid[doorRow + 1][MAZE_W - 1] = PATH;
      grid[doorRow + 1][MAZE_W - 2] = PATH;
  }
  // ... análogo para NORTH/SOUTH/WEST, con doorCol = MAZE_W / 2
  ```

- **Accesibilidad interna**: todas las celdas que el `carve()` llega a
  visitar quedan mutuamente alcanzables por construcción (es un único
  árbol de expansión — cada celda se marca `PATH` solo al ser
  alcanzada recursivamente desde la semilla). Pero el mecanismo de
  "forzar" una celda objetivo (`walls >= 2 || isForcedTarget(nx,ny)`,
  generalizado del `endX/endY` original a una lista de hasta 4
  objetivos, uno por puerta activa) **no garantiza al 100%** que el
  DFS llegue a visitar cada objetivo — solo evita saltárselo *si* el
  DFS ya alcanzó una celda adyacente a él. Verificado por fuzzing
  (45.000 combinaciones puerta×semilla, spec §11 Paso 3): ~0.17% de
  los casos dejan 1 puerta sin conectar. Arreglo aplicado: cada
  `ANCHOR_*` comparte fila o columna con la semilla del carve
  (`ROOM_SEED_COL/ROW`) por construcción, así que si un objetivo
  queda sin visitar tras el `carve()`, se traza un corredor recto de
  1 celda de ancho desde el objetivo hasta la semilla (`bridgeToSeed`
  en `maze.c`) — la semilla siempre es `PATH` (el `carve()` la marca
  incondicionalmente al empezar), así que el bridge siempre reconecta
  con éxito.
- **Posición de entrada del jugador**: al cruzar una puerta (§7), el
  jugador aparece en el hueco de 2 celdas del lado por el que entró,
  igual que hoy. En la Habitación de Inicio, al no venir de ningún
  lado, aparece en el centro de la habitación (que siempre es una
  celda de camino, por ser la semilla del carve).

## 6. Vista de Mapa Guía (botón C)

- Pulsar C (edge-triggered, igual que A/B en `main.c:40-43`) abre un
  overlay con una miniatura del Mapa Guía completo.
- Solo se dibujan celdas con `visited == TRUE`; el resto queda vacío
  (fog of war).
- La celda `(currentCol, currentRow)` se resalta. La Habitación de
  Salida **no** se marca de forma especial en el mapa hasta que el
  jugador la visite (mismo fog of war que cualquier otra habitación
  — no se le da una pista de dónde está la meta).
- Segundo toque de C (o botón de cancelar) vuelve al juego.
- `visited` se marca `TRUE` la primera vez que el jugador entra en
  esa celda (cruza la puerta), no al asomarse.

## 7. Transición entre habitaciones

1. El jugador sale del rango `0..MAZE_W-1 / 0..MAZE_H-1` por un lado
   con `door == TRUE` en `guideMap[currentRow][currentCol]`.
2. `currentCol`/`currentRow` se actualizan según la dirección.
3. Se regenera el maze interior de la Habitación destino con su
   `roomSeed` determinista (§5) — mismo resultado que la vez anterior.
4. El jugador se reposiciona en el hueco de 2 celdas del borde
   opuesto, alineado con la puerta simétrica.
5. `guideMap[currentRow][currentCol].visited = TRUE`.

**Nota (post-Paso 5, feedback del usuario):** la versión original de
esta spec incluía un paso 6 aquí — entrar en `(goalCol, goalRow)`
disparaba una "condición de victoria" que congelaba al jugador (dejaba
de llamarse `Player_updateRoom`). El usuario reportó esto como que "el
juego se congela al llegar a la última pantalla" y, tras preguntarle,
pidió explícitamente quitar el freeze y **seguir explorando
libremente** sin más lógica de fin de partida. Se eliminó por completo
la variable `won` de `main.c` — `(goalCol, goalRow)` sigue existiendo
como concepto de generación (§4.3, garantiza distancia mínima desde el
inicio) pero ya no se comprueba ni se actúa sobre ella en el bucle
principal. Entrar en esa habitación es indistinguible de entrar en
cualquier otra.

Cruzar por un borde sin puerta activa en esa dirección no es
posible: `Maze_isWall` en ese borde sigue devolviendo `TRUE`, igual
que hoy — el rebote actual se mantiene sin cambios ahí.

## 8. Presupuesto de memoria

| Dato | Tamaño (8x6 = 48 celdas) |
|---|---|
| `guideMap` (48 celdas × 1 byte empaquetado) | 48 B |
| Habitación actual (`grid[14][20]`) | 280 B |
| Buffer transitorio de generación (`frontier`/`queue`/`candidates`, reutilizable, solo durante `GuideMap_generate`) | ≤ 384 B |
| `dist[MAP_ROWS][MAP_COLS]` (solo durante selección de inicio/salida) | 48 B |

El buffer transitorio no necesita persistir tras generar el mapa —
puede vivir en una zona de RAM reutilizada luego para otra cosa (o en
el stack, si el espacio lo permite). Todo el sistema cabe con margen
amplio en los 64KB de RAM del 68000, incluso subiendo
`MAP_COLS`/`MAP_ROWS` más adelante — el coste crece linealmente con
el número de celdas, no con el contenido interior de cada habitación
(que nunca se almacena, solo se regenera).

## 9. Fuera de alcance de esta spec (v1)

- Loops/ciclos en el Mapa Guía (§0).
- Enemigos, coleccionables, HUD.
- Persistencia entre partidas (guardado en SRAM) — el `mapSeed` sí
  podría guardarse en SRAM en una v2, pero no está en el alcance
  actual.

## 6bis. Overlay con gráficos reales (reemplaza el texto ASCII del §6)

Tras ver una referencia visual del usuario (mapa estilo Metroid/Super
Metroid: habitaciones como cajas conectadas por pasillos, huecos exactos
donde hay puertas), se sustituyó el texto (`#`/`@`) de `GuideMap_drawOverlay`
por tiles pixel-art reales, manteniendo la limitación honesta de que **todas
las habitaciones del motor son del mismo tamaño** (a diferencia de la
referencia, donde varían) — decisión confirmada con el usuario.

- **Tileset nuevo** (`res/sprite/map_tiles.png`, 4 tiles de 8x8, mismos
  colores que `maze_tiles.png`): `WALL` (relleno sólido), `OPEN` (hueco =
  fondo), `CORRIDOR_H`/`CORRIDOR_V` (segmento de pasillo). Cargado en
  `TILE_USER_INDEX + MAZE_TILE_COUNT` (justo después del tileset del maze,
  `MAZE_TILE_COUNT=40` ahora expuesto en `maze.h` para evitar solapar VRAM).
- Cada habitación visitada se dibuja como una caja **sólida** de 3x2
  tiles (`WALL` en las 6 celdas, siempre — ver nota de corrección más
  abajo). La conectividad se muestra solo con el tile de pasillo en el
  hueco entre habitaciones contiguas (stride 4x3), cuando ambas están
  visitadas.
- **Corrección tras feedback del usuario**: la primera versión perforaba
  la propia caja según qué puertas estuvieran activas (columna/fila por
  lado). Con una caja de solo 3x2, cada puerta abierta borraba un tercio
  de la habitación — con 2-4 puertas activas quedaban fragmentos sueltos
  que se leían como letras rotas ("F", "L"), no como una sala. Se
  simplificó a caja siempre sólida; la conectividad la muestra solo el
  pasillo entre cajas, igual que un mapa Metroid real (la sala nunca se
  perfora a sí misma).
- **Resaltado de la habitación actual**: mismo tileset, pero con `PAL2`
  (fondo igual, color de "pared" distinto — amarillo brillante) en vez de
  `PAL0`. No hace falta un tile aparte.
- **Bug real corregido (índices de paleta invertidos)**: `rescomp` asigna
  el índice de paleta 0 al primer color que encuentra escaneando la
  imagen de arriba-izquierda a abajo-derecha. `maze_tiles.png` empieza
  con su celda de suelo/fondo, así que ahí índice0=fondo (de donde
  `maze.c` obtiene su convención `PAL_setColor(0,fondo)`). La primera
  versión de `map_tiles.png` empezaba con el tile WALL (violeta), así
  que rescomp asignó índice0=violeta e índice1=fondo — justo al revés
  de lo que las paletas (`PAL2`/`PAL3`) asumían. Efecto visible: las
  cajas sólidas salían invisibles, y los tiles de pasillo se veían como
  un bloque violeta con una raya oscura en medio (los patrones "II"/"="
  que reportó el usuario). Arreglo: reordenar el tileset (hueco/fondo
  primero, pared después) para que el primer píxel escaneado sea fondo,
  igual que `maze_tiles.png` — verificado contra los bytes compilados
  en `resources.s` antes de darlo por cerrado.
- **Iteraciones de estilo (feedback del usuario)**: primero relleno
  sólido plano (se leía como un blob, no una habitación); luego se
  probó rellenar con el tile de dither punteado del maze
  (`MAZE_WALL_DITHER_TILE` en `maze.h`, queda definido aunque hoy no se
  usa) — el usuario lo probó y pidió revertir, prefería el contorno de
  2px hueco de la iteración anterior.
- **Un solo color, sin PAL2/PAL3 (feedback del usuario)**: el usuario
  pidió usar únicamente "el color del tilemap" — el violeta ya
  establecido en todo el juego, `0x987DFA` (PAL0, el mismo que las
  paredes del maze). Se quitaron `PAL2` (resaltado amarillo de la
  habitación actual) y `PAL3` (gris de no-visitada); ahora **todas**
  las habitaciones (visitadas, no visitadas, actual) se dibujan con el
  mismo contorno violeta — ya no hay forma visual de distinguir la
  posición actual ni qué se ha visitado, solo por color. **Estado
  final**: contorno hueco de 2px (9 tiles en `map_tiles.png`: ancla de
  fondo + 6 piezas de borde TL/T/TR/BL/B/BR + 2 de pasillo), un único
  color violeta.
- **Posición actual marcada por forma, no por color**: con un solo
  color ya no hay manera de resaltar la habitación actual tintándola
  distinto. Se añadió un 10º tile (`MAP_TILE_FILL`, relleno sólido) que
  sustituye al contorno hueco **solo** en `(currentCol, currentRow)` —
  el resto de habitaciones conocidas siguen siendo el contorno de 2px.
  Mismo color violeta en ambos casos; la diferencia es forma (hueca vs
  sólida), no tinte.
- **Habitaciones visitadas marcadas (feedback del usuario)**: primero
  con 4 esquinas sólidas + bordes medios en blanco; el usuario pidió
  cambiarlo a un relleno estilo dithering. Estado final: rellenas con
  `MAZE_WALL_DITHER_TILE` (el mismo tile punteado de las paredes del
  maze, ya definido en `maze.h` — sin arte nuevo), dando una tercera
  textura distinta a la caja hueca (conocida, no visitada) y la caja
  sólida (actual). Las tres formas conviven, siempre en el mismo violeta.
- **Cambio de fog-of-war tras feedback del usuario**: ya no se oculta del
  todo una habitación no visitada — se dibuja igual (mismo tile sólido)
  pero con `PAL3` (gris apagado) en vez de violeta, y los pasillos se
  muestran entre cualquier par de habitaciones con puerta, visitadas o
  no. Solo las celdas `CELL_EMPTY` (las que nunca fueron habitación) no
  se dibujan. Esto revela la forma del mapa antes de explorarlo del
  todo — es una decisión explícita del usuario, no un descuido.
- **No verificado interactivamente**: igual que el resto del overlay,
  compila limpio y el ROM arranca sin errores en BlastEm, pero no se pudo
  automatizar una captura de pantalla del contenido real de BlastEm en
  este entorno (macOS no cede el foco de ventana a la automatización aquí).
  Revisar visualmente en BlastEm antes de darlo por cerrado.

## 10bis. Menú de inicio y tamaño configurable (añadido tras el Paso 5)

No estaba en el alcance original, pero se implementó tras completar los
5 pasos: una pantalla de título antes de `newGame()`.

- **Presets de tamaño** (`main.c`): Pequeño 6x4, Mediano 8x6 (el default
  original de la spec), Grande 10x8. Se ciclan con IZQUIERDA/DERECHA y
  se confirman con START.
- **Refactor de `guidemap.h`/`guidemap.c`**: `MAP_COLS`/`MAP_ROWS`
  (constantes de compilación) se dividieron en dos conceptos:
  - `MAX_MAP_COLS`/`MAX_MAP_ROWS` (10x8, fijas): solo dimensionan los
    arrays estáticos (`guideMap`, `frontier`, `dist`).
  - `mapCols`/`mapRows` (variables `extern u8`, en tiempo de ejecución):
    la rejilla realmente activa, usada en todos los bucles/comprobaciones
    de límites de `guidemap.c`. `main.c` las fija según el preset elegido
    **antes** de llamar a `GuideMap_generate()`.
- **Verificación**: se repitieron los harnesses de host de los Pasos 1-4
  (`test_guidemap.c`, `test_integration.c`) para los tres presets — 500
  semillas y 300 recorridos start→goal por preset, 0 fallos en los tres
  tamaños. `test_maze.c` no se vio afectado (usa `MAZE_W`/`MAZE_H`,
  ajenos a este cambio).
- **Estado no verificado interactivamente**: igual que el overlay del
  Paso 5, el renderizado del menú (`VDP_drawText`) y la navegación con
  D-pad/START no se probaron jugando — solo por compilación limpia y
  ausencia de errores en el log de BlastEm.

## 10. Orden de implementación sugerido

1. `guideMap` + `GuideMap_generate()` con Prim aleatorio (§3, §4.1),
   sin UI todavía — verificar por depuración que el árbol es conexo
   y que crece equilibrado en las cuatro direcciones, no en diagonal.
2. Poda de hojas (§4.2) y selección de inicio/salida por BFS (§4.3)
   — verificar que la distancia real entre `start` y `goal` es
   siempre ≥65% de la excentricidad hallada.
3. Semilla determinista por habitación + puertas forzadas
   generalizadas en `Maze_generate()` (§5) — probar con una
   habitación de 2, 3 y 4 puertas activas y confirmar que todas son
   mutuamente alcanzables.
4. Transición entre habitaciones (§7). (Nota: inicialmente incluía una
   condición de victoria al llegar a `goalCol/goalRow`; se quitó tras
   feedback del usuario — ver la nota en §7.)
5. Vista de Mapa Guía con botón C (§6) — la parte más visual, se deja
   para el final una vez la exploración funciona.

## 11. Checklist de verificación

Comprobaciones concretas antes de dar cada paso de §10 por cerrado.

### Paso 1 — `GuideMap_generate` (Prim)

- [ ] Cada `CELL_ROOM` generada tiene al menos 1 puerta activa
      (ninguna celda queda aislada).
- [ ] Las puertas son simétricas: si `guideMap[r][c].doorE == TRUE`,
      entonces `guideMap[r][c+1].doorW == TRUE` (y análogo N/S). Bug
      típico si se abre solo un lado de la puerta.
- [ ] El número de `CELL_ROOM` generadas coincide con las celdas
      realmente alcanzadas por el frontier (sin huecos perdidos ni
      duplicados).
- [ ] Repetir la generación con varios `mapSeed` y confirmar (contando
      `CELL_ROOM` por cuadrante N/E/S/O respecto al inicio) que el
      árbol no crece sistemáticamente hacia un solo lado — es la
      verificación de que Prim corrige el sesgo del DFS-backtracker.
- [ ] `frontierCount` nunca excede la capacidad reservada
      (`MAP_COLS*MAP_ROWS*4`) — instrumentar con un contador de
      máximo alcanzado en build de depuración.

### Paso 2 — Poda + selección start/goal

- [ ] Tras podar, el árbol sigue conexo (ninguna `CELL_ROOM` queda
      con 0 puertas).
- [ ] Solo se podan celdas que tenían exactamente 1 puerta antes de
      podar (nunca nodos internos).
- [ ] BFS: todas las `CELL_ROOM` terminan con `dist != 0xFF` — si
      alguna queda sin visitar hay una desconexión real (bug en el
      carve o en la poda).
- [ ] `goalCol/goalRow != startCol/startRow`.
- [ ] La distancia real por puertas entre `start` y `goal` es ≥65% de
      `maxDist` — comprobarlo recorriendo el camino en BlastEm, no
      solo confiar en el cálculo.
- [ ] Caso degenerado `maxDist == 0`: confirmar que hay guarda
      explícita para que `candidateCount == 0` no crashee.

### Paso 3 — Maze interior con puertas generalizadas

- [x] Determinismo: verificado por fuzzing (300 semillas, con estado
      de PRNG ambiental distinto antes de cada llamada) — mismo
      `roomSeed` produce siempre el mismo `grid[][]` byte a byte. 0
      fallos.
- [x] Caso límite de 4 puertas activas simultáneas: cubierto dentro
      del fuzzing de 45.000 combinaciones puerta×semilla (incluye la
      combinación N+E+S+W). 0 fallos tras el arreglo del bridge.
- [x] Los índices de punch-through no se salen de `grid[MAZE_H][MAZE_W]`
      con `MAZE_W=20`/`MAZE_H=14` — revalidar si cambian en el futuro.
- [x] Desde el punto de entrada del jugador, las puertas activas son
      alcanzables sin cruzar pared — 45.000 combinaciones puerta×semilla
      verificadas por flood-fill sobre `Maze_isWall()`. Encontrado y
      corregido un fallo real (~0.17% de los casos, el mecanismo de
      "forzar objetivo" no garantizaba el 100% — ver §5, `bridgeToSeed`).

### Paso 4 — Transición entre habitaciones

- [x] Cruzar cada una de las 4 direcciones reposiciona al jugador
      dentro del hueco de 2 celdas del lado opuesto, nunca sobre
      pared — verificado con 300 recorridos reales start→goal
      (`test_integration.c`), comprobando `Maze_isWall()` tras cada
      transición. 0 fallos.
- [x] Cruzar un borde sin puerta activa sigue rebotando igual que hoy:
      `Player_updateRoom` solo intercepta el cruce cuando la puerta de
      ese lado está activa; en cualquier otro caso cae al mismo
      `movePlayer()` (factorizado de `Player_update`, sin cambios de
      comportamiento).
- [x] ~~La condición de victoria se dispara exactamente al entrar en
      `(goalCol, goalRow)`~~ — eliminada tras feedback del usuario
      (ver nota en §7); ya no aplica. Verificado en su momento que
      0/300 recorridos la activaban antes de tiempo, antes de quitarla.
- [x] `visited` se actualiza en cada transición (incluida la
      habitación de inicio, marcada al cargarla) — verificado junto a
      lo anterior.

### Paso 5 — Vista de Mapa Guía (botón C)

- [x] Las habitaciones no visitadas no se dibujan (fog of war real,
      no solo atenuadas) — `GuideMap_drawOverlay` escribe `' '` para
      `visited==FALSE`, revisado por inspección de código.
- [x] El resaltado de `(currentCol, currentRow)` se actualiza tras
      cada transición — se le pasa `currentCol/currentRow` en cada
      llamada, no queda cacheado.
- [x] Abrir/cerrar el overlay con C no consume el edge-trigger de A/B
      en el mismo frame: los tres botones se comprueban con `if`
      independientes sobre el mismo `state`/`prevState`, ninguno
      hace `return`/`continue` que salte a los demás.
- [ ] **Sin verificar interactivamente**: no pude automatizar la
      entrada de teclado hacia la ventana de BlastEm en este entorno
      (macOS deniega permisos de accesibilidad a `osascript` aquí), así
      que el renderizado real del overlay (texto en pantalla, sprite
      oculto/visible) no se probó jugando — solo por compilación
      limpia, ausencia de errores en el log de BlastEm, y revisión de
      código contra la API documentada de SGDK (`VDP_drawText`,
      `VDP_clearPlane`, `SPR_setVisibility`). Recomendado: probar
      manualmente pulsando C en BlastEm antes de dar el paso por
      cerrado del todo.

## 12. Enemigos con ruta rutinaria (fuera del alcance original de esta spec)

Añadido tras completar el Mapa Guía: un enemigo por habitación,
patrullando en línea recta y rebotando en las paredes del maze —
sin colisión con la nave todavía (explícitamente pedido así).

- **`enemy.h`/`enemy.c`**: struct `Enemy { x, y, dir }` y
  `Enemy_update()` — misma colisión de caja de 16px contra el maze que
  `player.c`'s `movePlayer` (duplicada, no compartida, para no tocar
  código de player.c ya verificado extensamente).
- **`Enemy_spawnForRoom(e, roomSeed)`**: aparece en el centro de la
  habitación (mismo punto que `Player_spawnAtRoomCenter`) y la
  dirección inicial sale de `roomSeed & 3` — la misma semilla que ya
  usa `Maze_generateRoom` para esa celda, así que la ruta de patrulla
  es determinista y persiste al re-entrar a la habitación, igual
  filosofía que el propio layout del maze (spec §5).
- **Sprite**: `res/sprite/enemy.png` (16x16, mismo duotono violeta/
  fondo que el resto del juego — forma de "mina" con púas, distinta a
  la nave redondeada, para que se distinga por silueta sin necesitar
  otro color). Paleta propia (`PAL2`, libre desde que el Mapa Guía
  pasó a un solo color).
- **Fuera de alcance de este pase**: colisión nave-enemigo, tipos de
  enemigo distintos, daño/vidas.
- **Bordear bloques, no rebote aleatorio (feedback del usuario)**:
  primer intento: al chocar, 50% de probabilidad de girar hacia un
  lado al azar en vez de rebotar — el usuario lo rechazó explícitamente
  ("que rodeen los bloques no que cambien de dirección aleatoriamente").
  Se sustituyó por **wall-following determinista** (regla de la mano
  derecha/izquierda): `Enemy` gana `preferRight` (fijado una vez en
  `Enemy_spawnForRoom` desde `roomSeed`, nunca vuelto a sortear). Al
  chocar, siempre intenta girar primero hacia esa misma mano fija; si
  ese lado también está bloqueado, intenta el otro; si ambos lo están,
  rebota (callejón sin salida). Esto sí traza el contorno completo de
  un bloque en vez de zigzaguear.
- **Bug real corregido por fuzzing** (independiente del giro, ya
  existía en la versión de rebote simple): el enemigo podía salir por
  un hueco de puerta y terminar con coordenadas fuera del mapa, porque
  a diferencia de la nave no tiene lógica de transición de habitación.
  Arreglado en `enemy.c`'s `wallAt()`: el anillo exterior de la
  habitación siempre cuenta como pared para el enemigo, haya puerta o
  no. Verificado con 3,75 millones de pasos simulados (15 combinaciones
  de puertas × 50 semillas × 5000 pasos) — 0 fallos de límites o de
  atravesar paredes, antes y después del cambio a wall-following.
- **"Siempre un muro a la derecha" (feedback del usuario)**: el
  wall-following anterior solo decidía al chocar (probar mano
  preferida → la otra → rebote), lo cual no mantiene contacto
  continuo con la pared. Se cambió a la regla real de la mano
  derecha: **en cada paso**, no solo al chocar, prueba en orden girar
  a la derecha → seguir recto → girar a la izquierda → dar la vuelta,
  y toma la primera opción libre. Un hueco a la derecha se toma al
  instante en vez de pasar de largo, que es lo que de verdad mantiene
  un muro pegado al lado derecho de forma continua. Se quitó
  `preferRight` (ya no hace falta, siempre es la derecha). Reverificado
  con el mismo fuzzing de 3,75M de pasos — 0 fallos.
- **Bug real: se quedaba parado (feedback del usuario)**: la versión
  anterior, al decidir girar, solo cambiaba `dir` y esperaba al
  siguiente frame para moverse (igual que un rebote normal). En una
  zona abierta (como el punto de aparición, típicamente despejado),
  "girar a la derecha" seguía siendo válido frame tras frame desde
  cada nuevo rumbo, así que podía re-decidir girar indefinidamente sin
  avanzar nunca — visualmente parado, girando sobre sí mismo. Arreglo:
  `Enemy_update` ahora decide la dirección y **siempre** da un paso en
  esa dirección en la misma llamada, sin excepción. Verificado con
  1.500.000 llamadas simuladas: 0 casos de frame sin movimiento.
- **Bug real más profundo: atrapado en bucles cerrados (feedback del
  usuario)**: "a veces aparecen bloqueados" resultó ser un test de
  fuzzing dedicado (`test_enemy3.c`, cuenta celdas únicas visitadas en
  3000 pasos) confirmando que el 72% de las salas dejaban al enemigo
  dando vueltas en solo 1-4 celdas para siempre. Causa raíz: **cada
  habitación genera, por construcción, un bloque 2x2 totalmente abierto
  justo en el punto de aparición** (`carve()` en `maze.c` marca ese
  bloque como camino como primer paso, antes de tallar nada más) — es
  un bucle cerrado garantizado. Con "siempre intenta la derecha
  primero" en cada casilla, el enemigo queda atrapado ahí desde el
  frame 1: es una propiedad matemática del algoritmo de la mano en la
  pared (no puede salir de un anillo cerrado una vez dentro), no un
  bug de implementación. Además, la decisión se re-evaluaba a nivel de
  píxel (1px de holgura), lo cual en cualquier zona 2D abierta hacía
  que "girar a la derecha" pareciera libre en casi todos los frames,
  produciendo un giro de 4 pasos que se cancela a sí mismo
  (arriba-derecha-abajo-izquierda vuelve exactamente al píxel de
  partida) — doble causa del mismo síntoma.
  - Arreglo de granularidad: las decisiones de giro solo se toman al
    estar exactamente centrado en una casilla (`x`/`y` múltiplos de
    `MAZE_TILE_PX`), comprobando la **casilla completa** vecina
    (`tileBlockedInDir`), no solo 1px de holgura.
  - Arreglo de comportamiento: se preguntó al usuario el trade-off
    real (mano derecha estricta = atrapado en cualquier bucle cerrado,
    frente a recto por defecto = patrulla más la sala). Eligió
    **recto por defecto, gira solo si choca** (derecha → izquierda →
    vuelta atrás, determinista, sin aleatoriedad) — ya no es "mano
    pegada a la pared" continua, pero si bordea obstáculos reales
    cuando los encuentra.
  - Resultado tras el cambio: de 323/450 salas atrapadas (72%,
    típicamente en las 1-4 celdas del bloque de aparición) a
    **37/450 (8%)**, y esas pocas ahora son bucles reales de 8-14
    celdas propios de la geometría de esa sala concreta, no el bloque
    de aparición — una limitación aceptable y esperada del algoritmo
    con mapas que tienen ciclos, no un fallo sistemático.
- **Segundo enemigo, spawn aleatorio y colisión entre ellos (feedback
  del usuario)**: `ENEMY_COUNT=2` en `main.c` (array de `Enemy`/
  `Sprite*` en vez de una sola instancia). `Enemy_spawnForRoom` ya no
  aparece siempre en el centro de la sala — prueba hasta 32 casillas
  aleatorias (contra el stream de `random()` ya sembrado con
  `roomSeed` por `Maze_generateRoom`, así que sigue siendo
  determinista por sala) hasta encontrar una abierta, con el centro
  como respaldo si todas fallan. Cada frame se comprueba solape AABB
  16x16 entre cada par de enemigos (`Enemy_overlaps`); si se tocan,
  ambos invierten dirección (`Enemy_opposite`) — igual que rebotar en
  una pared, sin modelo de daño/vidas. Verificado: 0/1500 apariciones
  dentro de una pared; 12/1500 coincidencias de spawn en la misma
  casilla (raro pero inofensivo, el chequeo de colisión los separa en
  el primer frame).
- **Vuelta a mano derecha estricta (feedback del usuario, quería
  probarla)**: revertido `Enemy_update` a comprobar la derecha primero
  en cada casilla, antes de preguntar si el camino recto está
  bloqueado — el trade-off aceptado explícitamente sabiendo el riesgo.
  Remedido con el spawn aleatorio ya en su sitio (no el centro fijo de
  antes): **297/450 salas (66%)** siguen quedando atrapadas en un
  bucle pequeño en 3000 pasos — el spawn aleatorio apenas cambia el
  resultado (72%→66%) porque el problema no es solo el bloque 2x2 de
  aparición, sino que esta rejilla suele tener varios bucles pequeños
  alcanzables cerca de cualquier punto. Sin fallos de límites/paredes
  ni de "frame sin mover" — el riesgo aceptado es solo de patrulla
  limitada a una zona pequeña, no de un bug que rompa el juego.
- **Vuelta definitiva a "recto por defecto" (feedback del usuario)**:
  el 66% de salas atrapadas con mano derecha estricta no compensaba.
  Revertido a `Enemy_update` recto-por-defecto/gira-solo-si-choca.
  Con spawn aleatorio: 68/450 (15%) siguen quedando atrapadas en 3000
  pasos — más que el 8% medido antes de añadir el spawn aleatorio
  (algo esperable, más puntos de partida posibles rondando bucles
  pequeños), pero muy por debajo del 66-72% de la mano derecha
  estricta. Este es el estado final: `Enemy_update` en `enemy.c` no
  debería volver a tocarse sin repetir este mismo fuzzing antes.

## 13. Sistema de objetos (letras A-E)

Decisiones confirmadas con el usuario: 5 letras fijas (no escala con
el tamaño de mapa), orden de recogida forzado (no se puede coger la
letra N+1 sin haber recogido la N), recogida por contacto físico
(primera colisión jugador-objeto real del juego), HUD fijo en
pantalla durante toda la partida.

- **Selección de habitaciones** (`guidemap.c`, `selectItemRooms`,
  llamada al final de `GuideMap_generate` tras `selectGoal`): candidatas
  = habitaciones con exactamente 1 puerta ("sin salidas" = callejón sin
  salida), excluyendo la sala de inicio. Muestreo por dispersión de
  puntos más lejanos (farthest-point sampling): la letra 0 es la
  candidata más lejana del inicio; cada letra siguiente maximiza la
  distancia MÍNIMA a todas las ya elegidas (`minDistToChosen[]`,
  actualizado con un `bfsFromRoom()` extra por cada letra ya colocada —
  el BFS se generalizó de `bfsFromStart()` a un punto de partida
  arbitrario para esto). Verificado con 600 semillas (3 tamaños × 200):
  0 fallos — todas son callejones reales, ninguna coincide con el
  inicio ni entre sí, distancia mínima entre pares crece con el tamaño
  del mapa (peor caso 2 en 6x4, 6 en 10x8).
- **Estado de recogida** (`items.c`, nuevo módulo): `collected[]` +
  `nextIndex` (índice de la única letra recogible ahora mismo).
  `Items_tryCollect(col,row,playerX,playerY)` solo actúa si la
  habitación actual contiene la letra `nextIndex` Y la caja de 16x16
  de la nave se solapa con la posición fija del objeto (el mismo
  punto — `MAZE_DOOR_COL`/`MAZE_DOOR_ROW` — que usa el spawn del
  jugador en la sala de inicio, siempre camino garantizado). Tocar una
  letra futura antes de tiempo no hace nada — se queda ahí, visible,
  inerte.
- **Renderizado en la habitación**: `Items_drawInRoom` dibuja la letra
  con `VDP_drawText` (plano por defecto, `BG_A`, mismo plano que el
  maze) en la posición fija del objeto, solo si sigue sin recoger.
  Llamado tras cada `Maze_draw()` (en `loadRoom` y tras una recogida,
  para borrar la letra ya recogida redibujando el maze completo).
- **Marcado en el Mapa Guía**: `GuideMap_drawOverlay` dibuja la letra
  de cualquier objeto sin recoger sobre la caja de su habitación,
  **independientemente de si está visitada o no** — es la forma en
  que el jugador sabe dónde ir, tal como pidió.
- **HUD persistente**: `Items_drawHud` dibuja "A B _ _ _" en `BG_B`
  (plano separado del maze, que vive en `BG_A`) con prioridad alta
  (`VDP_setTextPriority(1)`) para que se vea por encima de los tiles
  de baja prioridad del maze en `BG_A` — sin esto, al ser planos
  opacos en Genesis, el maze taparía el HUD por completo. Se
  actualiza en `newGame()` y tras cada recogida exitosa.

## 14. Nave orientada hacia la dirección de movimiento ("patas primero")

El VDP del Genesis solo permite espejar sprites (flip horizontal/
vertical), no rotarlos — así que las 4 orientaciones "patas primero"
necesitan 2 artes dibujadas a mano, no 4:

- `res/sprite/player.png` (patas abajo, ya existente): sirve tal cual
  para `DIR_DOWN`, y con flip vertical (`SPR_setVFlip`) para
  `DIR_UP` — el arte no es simétrico arriba/abajo (el cuerpo ovalado
  se ensancha hacia abajo antes de las patas), así que el flip
  produce una orientación realmente distinta, no un no-op.
- `res/sprite/player_side.png` (patas a la derecha): generado
  rotando 90° el arte existente con ImageMagick (`-rotate -90`,
  sin antialiasing) en vez de dibujarlo desde cero, para mantener
  exactamente el mismo estilo y paleta. Sirve tal cual para
  `DIR_RIGHT`, y con flip horizontal para `DIR_LEFT`.
- `main.c`'s `syncPlayerSpriteOrientation()`: cambia de arte
  (`SPR_setDefinition`) y aplica el flip correspondiente solo cuando
  `player.dir` cambia respecto al frame anterior (evita llamadas
  redundantes). Se resetea (`playerVisualDir = 255`) en cada
  `newGame()` para forzar una sincronización inicial correcta.
- Confirmado en la compilación: `playerShipSide_palette_data` tiene
  el mismo contenido que `playerShip_palette_data` (misma paleta,
  como se esperaba de rotar el mismo arte) — no hace falta cargar
  paleta aparte para el sprite lateral, sigue usando `PAL1`.
- **Revertido (feedback del usuario)**: se quitó `syncPlayerSpriteOrientation`
  de `main.c`, la entrada `playerShipSide` de `resources.res`, y se
  borró `res/sprite/player_side.png` (sin uso). La nave vuelve a
  usar siempre el sprite original sin rotar. Nota de mantenimiento:
  al borrar el `.png`, `make` falló en seco (exit 2, sin mensaje)
  porque el `.d` de dependencias generado seguía listando el archivo
  borrado como prerequisito — hizo falta `make clean` antes de
  reconstruir. Si se vuelve a borrar un asset referenciado en un
  build anterior, repetir `make clean` primero.

## 15. Mapa Guía: intento de vista "solo la ruta", revertido (feedback del usuario)

Iteración 1 (rechazada): `GuideMap_drawOverlay` seguía mostrando el
mapa completo explorado, solo cambiando qué letras se revelaban
(`Items_revealedOnMap`, índices `<= nextIndex`). El usuario aclaró que
no era eso: quería que se pintase **solo la ruta** (el camino de
habitaciones/pasillos) hasta la letra desbloqueada, no el mapa
completo con más o menos letras visibles.

Iteración 2 (implementada y luego revertida): se probó una vista de
"solo la ruta", con estas decisiones confirmadas antes de
implementar — ruta trazada desde la posición actual de la nave (no
desde el inicio), solo se muestra la ruta y el resto del mapa se
oculta por completo, y los tramos de pasillo de la ruta usan la misma
forma sólida (`MAP_TILE_FILL`) que la sala actual. Se implementó vía
`tracePathTo()` (reconstrucción de camino único en el árbol de
habitaciones, caminando del destino al origen por `dist` decreciente
de `bfsFromRoom`) e `Items_currentTarget()` en lugar de
`Items_revealedOnMap`.

**Revertido (feedback del usuario: "revierte, enseña todo el
mapa")**: se volvió a la vista de mapa completo, con revelado
progresivo de letras, que es el comportamiento final/actual:

- `GuideMap_drawOverlay` vuelve a iterar sobre toda la rejilla
  (`mapCols` × `mapRows`), dibujando cada `CELL_ROOM` según su estado
  (relleno sólido si es la sala actual, dithering si está visitada,
  borde hueco si no) más los tramos de pasillo finos normales
  (`MAP_TILE_CORRIDOR_H`/`_V`) por cada puerta abierta — ya no hay
  filtrado por ruta. `tracePathTo()` y sus estáticos auxiliares
  (`pathEastSolid[][]`, `pathSouthSolid[][]`) se eliminaron del todo.
- `items.c` vuelve a exponer `Items_revealedOnMap(col,row,&letter)`
  (letras con índice `<= nextIndex`: las ya recogidas quedan como
  rastro, la actual pendiente se revela, las futuras siguen ocultas).
  `Items_currentTarget()` se eliminó (ya no tiene uso).
- Verificado: build limpio (`make`) y arranque limpio en BlastEm sin
  errores en el log tras el revert.

## 16. Bloqueo físico de zonas no accesibles

Petición del usuario: si toca buscar C, las ramas que solo llevan a D y
E deben estar bloqueadas (no solo el mapa lo indica — no se puede
caminar hasta allí), mientras que A y B (ya desbloqueadas antes) siguen
abiertas. A está desbloqueada desde el principio.

Decisiones confirmadas (preguntadas antes de implementar):
- **Alcance**: se bloquea toda la rama del árbol hacia una letra, no
  solo la puerta de su propia sala sin salida — si una sala
  intermedia solo lleva a letras aún no debidas, esa sala entera
  (y todo lo que cuelgue de ella) queda sellada, no solo el último
  tramo.
- **Percepción física**: la puerta bloqueada se ve distinta (tile de
  "puerta cerrada", no un muro genérico indistinguible) y colisiona
  exactamente como un muro — la nave rebota al chocar.
- **Mapa Guía**: las salas bloqueadas también se marcan de forma
  distinta en el overlay del botón C, no solo en el juego.

Implementación:
- **`res/sprite/maze_tiles.png`** pasa de 160x16 (10 celdas) a 176x16
  (11 celdas): se añade la celda 10, un patrón de reloj de arena/X en
  las mismas dos únicas colores del resto del tileset (`#252525` fondo,
  `#987DFA` violeta) — distinto a propósito de los 9 patrones de
  dithering ya existentes, para que se lea como "bloqueado" y no como
  "otra textura de muro más". `MAZE_TILE_COUNT` (maze.h) sube de 40 a
  44 (11 celdas × 4 subtiles) — desplaza automáticamente
  `MAP_TILE_BASE` en guidemap.c, que ya se calcula a partir de esa
  constante.
- **`maze.c`**: `Maze_generateRoom` gana 4 parámetros nuevos
  (`lockedN/E/S/W`, paralelos a `doorN/E/S/W`, solo válidos donde el
  `doorX` correspondiente es TRUE). El carving interior no cambia; solo
  el punch-through del borde de una puerta bloqueada usa la nueva celda
  `LOCKED_DOOR` (10) en vez de `PATH` (0). Como `Maze_isWall()` ya
  devuelve TRUE para cualquier celda `!= PATH`, la colisión del muro
  bloqueado sale gratis del código existente — no hizo falta tocar
  `player.c` en absoluto: el jugador nunca llega a pisar el tile
  límite que dispara `EXIT_NORTH/EAST/SOUTH/WEST` en
  `Player_updateRoom`, porque el movimiento ya se frena antes por
  colisión normal.
- **`guidemap.c`** (spec central de este bloqueo): `GuideMap_recomputeLocks()`
  recorre el árbol de habitaciones en dos pasadas —
  `computeContainsUnlocked()` (post-orden: ¿la subrama de esta sala
  contiene algún ítem con índice `<= nextIndex`?) y `markLocked()` /
  `markSubtreeLocked()` (pre-orden desde el inicio: si la subrama de
  un hijo no contiene nada debido, esa subrama entera se marca
  `roomLocked`, si sí lo contiene se sigue bajando por ahí). Como el
  grafo de habitaciones es un árbol, cada rama bloqueada solo toca el
  resto por una única arista — bloquear esa arista ya sella todo lo
  que cuelga de ella, sin necesitar guardar qué arista es
  explícitamente. `Items_isUnlocked(index)` (items.c/.h, nuevo) expone
  el mismo umbral `index <= nextIndex` que ya usaba
  `Items_revealedOnMap`, para que guidemap.c no dependa de
  `collected[]`/`nextIndex` directamente.
- **`main.c`**: `GuideMap_recomputeLocks()` se llama en `newGame()`
  (tras `GuideMap_generate()` + `Items_reset()`, así A queda
  desbloqueada desde el principio) y de nuevo cada vez que
  `Items_tryCollect()` devuelve TRUE. `loadRoom()` calcula
  `lockedN/E/S/W` de la sala que va a generar consultando
  `GuideMap_isRoomLocked()` sobre cada vecino con puerta, y se lo pasa
  a `Maze_generateRoom()`.
- **Overlay del Mapa Guía**: `GuideMap_drawOverlay` añade un tercer
  estado (entre "sala actual" y "visitada") para
  `GuideMap_isRoomLocked() == TRUE`: relleno con la misma textura de
  puerta bloqueada (`MAZE_LOCKED_DOOR_TILE`, la subtile representativa
  de la celda 10, reutilizada igual que `MAZE_WALL_DITHER_TILE` ya se
  reutilizaba para "visitada"). Una sala bloqueada nunca puede estar
  visitada a la vez (el desbloqueo es monótono: `nextIndex` solo
  avanza), así que no hay conflicto de prioridad entre ramas.
- **Verificado en el host** (`test_locks.c`, ~6000 generaciones válidas
  entre semillas 1-2000 × 3 tamaños de preset): para cada paso de
  `nextIndex` de 0 a `ITEM_COUNT-1`, el ítem actualmente debido nunca
  está bloqueado, todos los posteriores sí, la sala de inicio nunca
  está bloqueada, y el desbloqueo es estrictamente monótono (ninguna
  sala vuelve a bloquearse tras desbloquearse) — 0 fallos. Se
  descubrió (y se excluyó del test, no es un bug de este bloqueo) un
  problema preexistente en `selectItemRooms`: en el preset más pequeño
  (6x4), su fallback de "no hay suficientes salas sin salida" puede
  asignar dos letras a la misma sala (normalmente la de inicio) en
  ~0.5% de las semillas, lo que ya rompía la recogida en orden estricto
  antes de esta feature — no se ha tocado, queda anotado aquí por si se
  quiere arreglar en otra sesión.
- Verificado en BlastEm: build limpio, arranque limpio sin errores en
  el log tras el rebuild.

## 17. Fog of war estricto: solo se ve lo que la nave ha visitado

Petición del usuario: el Mapa Guía (botón C) debe ocultar las salas en
las que la nave no ha estado — el mapa se va abriendo a medida que se
avanza, no se muestra de golpe. Esto sustituye la vista de "mapa
completo" del §15 (que mostraba toda sala conocida, visitada o no, con
un borde hueco para las no visitadas) por una niebla de guerra real.

Decisiones confirmadas (preguntadas antes de implementar):
- **Letras (spec §13)**: también se ocultan hasta visitar la sala —
  dejan de actuar como baliza a través de la niebla; encontrarlas es
  parte de explorar.
- **Pasillos**: un pasillo solo se dibuja si **ambas** salas que
  conecta han sido visitadas — no se insinúan salidas hacia salas aún
  no vistas.

Implementación (`guidemap.c`, `GuideMap_drawOverlay`):
- Se añade `if (!cell.visited) continue;` justo después del filtro de
  `CELL_ROOM`, antes de decidir nada más — una sala no visitada no
  dibuja ni caja, ni pasillos hacia ella, ni su letra si la tuviera.
- Como consecuencia, el estado de 4 ramas que había quedado tras el
  §16 (actual / bloqueada / visitada / conocida-sin-visitar) se
  reduce a 2 (actual / visitada): una sala bloqueada (spec §16) nunca
  puede estar visitada — el desbloqueo es monótono — así que ya está
  cubierta por el mismo `continue`, y la rama que dibujaba
  `MAZE_LOCKED_DOOR_TILE` en el overlay se ha quitado por
  inalcanzable. La rama de borde hueco para "conocida pero no
  visitada" (`MAP_TILE_CORNER_TL` etc.) también se quita por el mismo
  motivo — ya no existe ese estado. El bloqueo físico en la propia
  habitación (§16: puerta con textura de candado, colisión de muro)
  no cambia en absoluto, esto es puramente sobre qué se ve en el
  overlay del mapa.
- Los tramos de pasillo (`MAP_TILE_CORRIDOR_H`/`_V`) ganan una
  condición extra: `guideMap[row][col+1].visited` /
  `guideMap[row+1][col].visited` sobre el vecino, además de la
  puerta y el límite de la rejilla que ya comprobaban.
- La letra del ítem (`Items_revealedOnMap`) no necesitó cambiar su
  propia condición — ya queda cubierta por el `continue` de arriba,
  puesto que para cuando el código llega a esa comprobación
  `cell.visited` ya es TRUE por construcción.
- Los tiles de borde hueco (`MAP_TILE_CORNER_TL/EDGE_T/CORNER_TR/CORNER_BL/EDGE_B/CORNER_BR`)
  se dejan definidos en `guidemap.c` y presentes en `map_tiles.png`
  aunque ya no se dibujen — es arte de tileset compartido, no vale la
  pena tocar la imagen ni el rescomp para esto.
- Verificado: build limpio (`make`, sin warnings) y arranque limpio en
  BlastEm sin errores en el log tras el rebuild.

## 18. Cada sección del mapa, un color de muros distinto (mientras se juega)

Petición del usuario: pintar cada "sección"/"path" de un color
distinto. Aclaración tras preguntar: se refiere al fondo del propio
juego (las habitaciones en sí, mientras se navega), no al overlay del
Mapa Guía del botón C.

Decisiones confirmadas (preguntadas antes de implementar):
- **Sección** = cada rama que sale directamente de la sala de inicio
  (como mucho 4, una por dirección N/E/S/W) — todas las salas que
  cuelgan de esa rama, incluidas sus propias sub-ramas, comparten
  color. La sala de inicio en sí usa el color de la sección 0.
- **Qué cambia**: los muros (el dithering), no el suelo — el suelo
  sigue siendo el fondo oscuro plano, sin textura, como hasta ahora.
- **Si hay más secciones que colores**: ciclar — en la práctica nunca
  ocurre, ver más abajo.

Descubrimiento durante el diseño: mi primera estimación de "solo ~2
colores disponibles sin tocar las paletas de nave/enemigos" (PAL0 +
PAL3) era demasiado conservadora — confundí el límite real (4 bancos
de paleta de hardware compartidos entre fondo y sprites) con cuántos
colores caben DENTRO de un solo banco. Cada banco de paleta del
Genesis tiene 16 colores; el tileset del maze solo usa 2 de esos 16
(fondo oscuro e índice 1) porque el arte original solo tiene 2 colores
— nada impide añadir arte nuevo que use los índices 2-15 del MISMO
banco (PAL0), sin tocar en absoluto PAL1/PAL2 (nave/enemigos, en uso
real mientras se juega) ni PAL3. Con eso, hay sitio de sobra para las
4 secciones (como mucho 4, ver arriba) sin ciclar nunca y sin ningún
riesgo de contaminar los colores de los sprites ni del texto (que por
defecto también usa PAL0, pero solo lee el índice 1, que no se toca).

Implementación:
- **`res/sprite/maze_tiles.png`** pasa de 176x16 (11 celdas) a 608x16
  (38 celdas): celda 0 = suelo; celdas 1-9, 10-18, 19-27, 28-36 = los
  mismos 9 patrones de dithering de siempre, repetidos una vez por
  sección con el color violeta reemplazado (`magick ... -opaque
  '#987DFA'`) por cada nuevo tono — mismo patrón/densidad de ruido
  exacto, un color distinto; celda 37 = la puerta bloqueada (spec
  §16), movida al final y sin reasignar de color (siempre el mismo
  aspecto, sea cual sea la sección, para que "bloqueado" se lea igual
  en todo el mapa). Colores nuevos: `#4AECC4` (verde azulado),
  `#FFA53E` (naranja), `#E85D75` (rosa), añadidos a los índices 2, 3 y
  4 de PAL0 — verificado escaneando la imagen final en orden raster
  (mismo método que el bug de índice0 documentado en el §4bis/guidemap.c)
  para confirmar el orden real de asignación antes de escribir los
  `PAL_setColor` en `Maze_loadGraphics()`.
- **`maze.c`**: `MAZE_TILE_COUNT` sube a 152 (38 celdas × 4 subtiles).
  Nueva variable estática `wallHueBase` (0/9/18/27), fijada al
  principio de `Maze_generateRoom()`/`Maze_generate()`;
  `randomWallVariant()` le suma el offset antes de devolver la celda —
  el resto del carving (`carve()`, `bridgeToSeed()`, anchors de
  puerta) no sabe ni le importa qué sección es, sigue operando sobre
  "valores de celda" sin significado de color. `Maze_generateRoom`
  gana un parámetro `u8 sectionHue` (0..3); `Maze_generate()` (el
  prototipo de una sola sala, sin tocar por el resto de esta spec)
  fija `wallHueBase = 0` explícitamente, sigue siempre violeta.
- **`guidemap.c`**: `computeSections()` (llamada una vez dentro de
  `GuideMap_generate()`, justo después de `pruneLeaves()` — es
  puramente estructural, depende solo de la topología final de
  puertas, no de items ni de bloqueos, así que no hace falta
  recalcularla nunca más, a diferencia de `GuideMap_recomputeLocks()`)
  recorre cada una de las puertas de la sala de inicio en orden
  N/E/S/W y hace un flood-fill (`floodSection`) de un número de
  sección distinto por cada rama directa. `GuideMap_roomSection(col,row)`
  expone el resultado. `MAZE_WALL_DITHER_TILE` (usado por el overlay
  del mapa para el relleno de "visitada") pasa de constante a macro
  con parámetro `(hue)`; el overlay siempre lo llama con `(0)` — el
  color por sección es una función solo de la vista de juego, el mapa
  del botón C se queda como estaba, en un único color.
- **`main.c`**: `loadRoom()` consulta `GuideMap_roomSection(col,row)` y
  se lo pasa a `Maze_generateRoom()`.
- **Verificado en el host** (`test_sections.c`, ~6000 generaciones
  entre semillas 1-2000 × 3 tamaños de preset): la sala de inicio
  siempre es sección 0; cada rama directa de la sala de inicio recibe
  un número de sección distinto de las demás; un recorrido BFS
  independiente (no usa el código interno de `computeSections()`, solo
  el grafo de puertas) confirma que todas las salas alcanzables dentro
  de una misma rama comparten esa sección; toda `CELL_ROOM` del mapa
  queda cubierta por alguna sección — 0 fallos.
- Verificado en BlastEm: build limpio (`make`, sin warnings), orden de
  colores confirmado contra la imagen final compilada, arranque limpio
  sin errores en el log tras el rebuild.

## 19. Diagnóstico "sigue todo violeta" y puertas bloqueadas sin icono propio

El usuario reportó, tras probar el §18, que el mapa seguía viéndose
todo violeta incluso explorando varias salas. Antes de tocar código
se verificó exhaustivamente que la implementación del §18 era
correcta: se decodificaron a mano los bytes de paleta compilados en
`out/release/res/resources.s` para las celdas de cada hue (índices
0/2, 0/3, 0/4 confirmados para teal/naranja/rosa respectivamente,
justo lo esperado) y se re-ejecutó `test_sections.c` (0 fallos). Se
añadió temporalmente un texto de depuración (`"col,row SECn"` en la
esquina superior izquierda de cada sala) para descartar a ciegas un
problema de percepción de color frente a un bug real de datos — se
quitó de nuevo en cuanto se identificó la causa real.

Causa real: el usuario estaba mirando una **puerta bloqueada** (spec
§16), que por diseño explícito de esa sección **siempre se dibujaba
violeta** (el icono de reloj de arena/X, con su propio color fijo,
independiente de la sección de la sala) — no era un bug del §18, sino
el comportamiento intencional de una pieza distinta del juego que el
§18 nunca tocó a propósito.

Decisión del usuario al confirmarlo: quitar el icono distintivo de
las puertas bloqueadas. Ahora una puerta sellada se pinta **igual que
cualquier otro muro de esa sala** — mismo dithering, mismo color de
sección — sin ningún indicador visual propio. Esto revierte
deliberadamente la elección original del §16 ("Muro invisible con
tile distinto") a favor de la opción que entonces se descartó ("Muro
normal, indistinguible"): la única forma de saber que una puerta está
bloqueada vuelve a ser chocar contra ella (o el propio Mapa Guía,
donde esa sala nunca se llega a ver por el fog of war del §17).

Implementación:
- **`res/sprite/maze_tiles.png`** vuelve a 592x16 (37 celdas): se
  quita la celda 37 (el reloj de arena) por completo.
  `MAZE_TILE_COUNT` baja a 148, `CELL_ROW_TILES` a 74.
- **`maze.c`**: la celda `LOCKED_DOOR` fija desaparece.
  `Maze_generateRoom()` ahora rellena el tramo de una puerta
  bloqueada con `randomWallVariant()` independiente por celda (igual
  que cualquier otro tramo de muro), en vez de un valor de celda fijo
  — automáticamente hereda el `wallHueBase` (spec §18) ya activo para
  esa sala, así que encaja con el color de sección sin lógica
  adicional. `Maze_isWall()` sigue bloqueando el paso igual (cualquier
  valor de celda `!= PATH` cuenta como muro), la física no cambió en
  absoluto — solo el aspecto.
- **`maze.h`**: se elimina `MAZE_LOCKED_DOOR_TILE` (ya estaba sin uso
  desde el §17, que había quitado su único consumidor en
  `guidemap.c` al ocultar las salas bloqueadas del todo bajo el fog
  of war — confirmado con `grep` antes de borrarla).
- **Verificado**: build limpio (`make`, sin warnings), recuento de
  tiles compilados confirmado en 148, orden de colores re-verificado
  contra la imagen final, `test_locks.c` y `test_sections.c`
  re-ejecutados sin fallos (la lógica de `guidemap.c` no cambió en
  este paso), arranque limpio en BlastEm.

## 20. Baliza de la siguiente letra a través de la niebla

Petición del usuario: mostrar en el Mapa Guía dónde está la siguiente
letra. Como el fog of war del §17 oculta por completo cualquier sala
no visitada (incluida su letra, si la tuviera), y la letra pendiente
casi siempre vive en una sala que aún no se ha pisado, hacía falta
una excepción puntual.

Decisión confirmada (preguntada antes de implementar): esa sala
**solo** revela su letra, flotando en su posición — nada de caja ni
pasillos, no se revela la forma de la sala ni sus conexiones, solo
"aquí es". El resto de la niebla de guerra sigue exactamente igual
que en el §17.

Implementación:
- **`items.c`/`.h`**: nuevo `Items_isNextTarget(col,row,&outLetter)`
  — a diferencia de `Items_revealedOnMap` (que da TRUE para
  `índice <= nextIndex`, o sea la letra actual Y todas las ya
  recogidas), este exige `índice == nextIndex` exactamente: solo la
  UNA letra pendiente ahora mismo, nunca las ya recogidas.
- **`guidemap.c`**, `GuideMap_drawOverlay`: dentro de la rama
  `if (!cell.visited)` (antes solo hacía `continue`), se comprueba
  `Items_isNextTarget` y si es TRUE se dibuja únicamente el carácter
  de la letra con `VDP_drawText` en la posición de esa celda, antes
  del `continue` — ninguna otra parte de la sala se toca. Las salas
  visitadas siguen su camino normal sin cambios (su letra, si la
  tienen, ya se revela por `Items_revealedOnMap` como hasta ahora).
- **Verificado en el host** (`test_beacon.c`, ~6000 generaciones):
  para cada paso de `nextIndex` de 0 a `ITEM_COUNT-1`, hay exactamente
  una sala en todo el mapa con baliza activa, siempre coincide con la
  posición real de `itemCol[nextIndex]/itemRow[nextIndex]`, y la letra
  mostrada es la correcta; tras recoger todas las letras, ninguna sala
  tiene baliza — 0 fallos.
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 21. Todas las letras y su habitación, siempre visibles

Petición del usuario: "pinta todas las letras y su habitación" —
supera lo del §20 (solo la letra pendiente, sin caja) en dos sentidos:
**todas** las letras (no solo la siguiente pendiente, también las
bloqueadas más adelante en el orden), y con **su habitación**
(caja/forma de la sala, no solo el carácter flotando).

Implementación (`guidemap.c`, `GuideMap_drawOverlay`):
- La rama `if (!cell.visited) continue;` del bucle principal vuelve a
  ser un `continue` simple (spec §17, sin excepciones) — el fog of war
  para salas normales no cambia en nada.
- Se añade un **segundo bucle**, tras el principal, que recorre
  directamente `itemCol[]/itemRow[]` (solo `ITEM_COUNT`=5 iteraciones,
  no toda la rejilla): para cada letra, si su sala **ya** fue dibujada
  por el bucle principal (`guideMap[irow][icol].visited`), se salta —
  ya tiene su aspecto normal (sólido si es la actual, dithering si
  está visitada) y su letra ya se revela por `Items_revealedOnMap`
  como siempre. Si la sala **no** ha sido visitada, se dibuja con el
  borde hueco 3x2 (`MAP_TILE_CORNER_TL` etc. — el mismo aspecto
  "conocida, sin visitar" que existía antes del fog of war del §17,
  ahora reservado solo para estas 5 salas) más su letra encima, **sin
  mirar si está bloqueada ni si es la pendiente actual** — las 5
  siempre se pintan.
- `Items_isNextTarget` (introducido en el §20) queda sin uso — se
  borra de `items.c`/`.h` junto con su declaración, ya no hace falta
  ninguna distinción "es la siguiente" para esto: basta con recorrer
  `itemCol[]/itemRow[]` directamente, que guidemap.c ya tenía a mano.
- **Verificado en el host**: recompilados y re-ejecutados
  `test_locks.c` (5984 runs) y `test_sections.c` (6000 runs) sin
  fallos — la lógica estructural de `guidemap.c` no cambió, solo el
  renderizado del overlay. Se añadió `test_overlay_smoke.c` (3000
  llamadas a `GuideMap_drawOverlay` en distintos puntos de la
  recogida y desde distintas salas "actuales", con los stubs de VDP
  del host) para confirmar que el nuevo segundo bucle no revienta
  límites de array ni nada por el estilo — sin crashes.
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 22. El sprite de la nave marca la posición en el Mapa Guía

Petición del usuario: pintar el sprite de la nave (en pequeño) para
marcar dónde está en el Mapa Guía, en vez de (o además de) el relleno
sólido actual de la sala actual.

Decisiones confirmadas (preguntadas antes de implementar):
- El hardware del Genesis no escala sprites — se reutiliza el sprite
  `playerShip` (16x16px) tal cual, sin arte nuevo; encaja bien dentro
  de la caja de sala del mapa (24x16px, `ROOM_BOX_W`×`ROOM_BOX_H` en
  tiles de 8px).
- El relleno sólido (`MAP_TILE_FILL`) de la sala actual **se
  mantiene** — la nave se dibuja encima, no lo sustituye.

Implementación:
- **`guidemap.c`/`.h`**: se extrae `roomBoxOriginTiles(col,row,&rx,&ry)`
  (estática) de la lógica que ya calculaba `offsetX/offsetY/rx/ry`
  dentro de `GuideMap_drawOverlay` — ahora única fuente de verdad,
  usada por las dos pasadas de la función (antes tenían el cálculo
  duplicado). Nueva función pública `GuideMap_roomBoxPixelPos(col,row,&outX,&outY)`
  la reutiliza y escala de unidades de tile (8px) a píxeles, para que
  `main.c` pueda posicionar un sprite sin duplicar esta aritmética.
- **`main.c`**: al abrir el mapa (botón C), en vez de ocultar
  `playerSprite` (como antes), se llama a `GuideMap_roomBoxPixelPos`
  con `(currentCol,currentRow)` y se reposiciona el sprite ahí con
  `SPR_setPosition(playerSprite, mx + 4, my)` — el `+4` centra
  horizontalmente el sprite de 16px dentro de la caja de 24px
  (`(24-16)/2`); verticalmente coincide exacto (caja y sprite miden
  16px de alto), sin offset. Los sprites de los enemigos se siguen
  ocultando igual que antes. Al cerrar el mapa no hace falta
  restaurar la posición a mano: el bucle principal ya hace
  `SPR_setPosition(playerSprite, player.x, player.y)` cada frame
  que `!mapViewOpen`, lo que incluye el mismo frame en que se cierra.
- **Verificado en el host**: recompilados y re-ejecutados
  `test_locks.c`, `test_sections.c` y `test_overlay_smoke.c` tras el
  refactor de `roomBoxOriginTiles` — sin fallos ni crashes (la
  refactorización no cambió ningún resultado, solo evitó la
  duplicación del cálculo).
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 23. Nave en blanco parpadeante + salas visitadas con relleno sólido

Dos peticiones del usuario en el mismo mensaje:
1. El sprite de la nave en el mapa (§22), en blanco y parpadeando.
2. Las salas visitadas se pintan con relleno sólido, no con
   dithering.

Implementación (parte 1, `main.c`):
- `playerShip.palette` (spec §13bis, resources.res) es duotono: índice
  0 nunca se renderiza (el hardware de sprites del Genesis siempre
  trata el índice 0 como transparente) e índice 1 es el color visible
  real de la nave. `PLAYER_SHIP_INK_INDEX` calcula el índice CRAM
  absoluto de `PAL1`+índice1 (`(PAL1 * 16) + 1`, ya que `PAL_setColor`
  usa índices absolutos 0-63 sobre las 4 paletas de 16 colores cada
  una, no relativos por paleta).
- Al abrir el mapa: `PAL_setColor(PLAYER_SHIP_INK_INDEX, RGB24_TO_VDPCOLOR(0xFFFFFF))`
  pinta ese único índice de blanco (el índice 0, transparente, no se
  toca — no hace falta). Se resetea `mapBlinkTimer` y se fuerza
  visible como estado inicial del parpadeo.
- Cada frame mientras `mapViewOpen` es TRUE: `mapBlinkTimer` cuenta
  hasta `MAP_BLINK_FRAMES` (15, medio segundo de ciclo completo a
  60fps) y al llegar alterna `SPR_setVisibility` consultando el estado
  actual vía `SPR_isVisible(playerSprite, FALSE)` (sin forzar
  recálculo, solo lee el último flag puesto).
- Al cerrar el mapa: `PAL_setColor(PLAYER_SHIP_INK_INDEX, playerShip.palette->data[1])`
  restaura el color original de la nave (un único word, no hace falta
  recargar la paleta entera con `PAL_setPalette`).

Implementación (parte 2, `guidemap.c`):
- Como la nave ya marca la posición actual (parpadeando, en blanco),
  la distinción de forma entre "sala actual" (relleno sólido) y "sala
  visitada" (dithering) deja de tener sentido — se fusionan las dos
  ramas en una sola: cualquier sala visitada se pinta con
  `MAP_TILE_FILL`, sin excepción.
- Como consecuencia, `GuideMap_drawOverlay` ya no necesita saber cuál
  es la sala actual — pierde sus parámetros `(curCol, curRow)`, pasa a
  `GuideMap_drawOverlay(void)`. Actualizado el único call site en
  `main.c` y la declaración en `guidemap.h`.
- `MAZE_WALL_DITHER_TILE(hue)` (maze.h) se queda sin uso en
  `guidemap.c` tras este cambio, pero no se borra: los tiles que
  referencia siguen siendo el dithering real de los muros del juego
  (usados constantemente por `maze.c`), no es arte huérfano — solo un
  alias de acceso que ya no hace falta aquí, sin coste dejarlo.
- **Verificado en el host**: recompilados y re-ejecutados
  `test_locks.c` (5984 runs), `test_sections.c` (6000 runs) y
  `test_overlay_smoke.c` (3000 runs, actualizado a la nueva firma sin
  argumentos) — sin fallos ni crashes.
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 24. Parpadeo más lento + nave del mapa del tamaño de una letra

Dos peticiones del usuario en el mismo mensaje:
1. El parpadeo del §23, más lento.
2. La nave del mapa, del mismo tamaño que las letras (8x8px) — el
   sprite `playerShip` es de 16x16 (2x2 tiles) y el hardware no
   escala sprites, así que hace falta un sprite nuevo más pequeño.

Decisión confirmada (preguntada antes de implementar): silueta nueva
simplificada de la nave a 8x8px (1 tile), en vez de reutilizar un
carácter de texto existente como marcador genérico — sigue siendo un
sprite VDP real, solo más pequeño, no un símbolo desconectado de la
nave.

Implementación:
- **Parpadeo**: `MAP_BLINK_FRAMES` (main.c) pasa de 15 a 30 — medio
  segundo por estado en vez de cuarto de segundo, un segundo de ciclo
  completo a 60fps en vez de medio.
- **`res/sprite/map_ship.png`** (nuevo): 8x8px, mismo duotono que
  `player.png`/`enemy.png` (`#252525` fondo/transparente, `#987DFA`
  tinta) — un círculo relleno inscrito en el recuadro, silueta
  redondeada reconocible como "blob"/nave a ese tamaño, generado con
  ImageMagick (`-draw "circle 3.5,4 3.5,0.5"`).
- **`res/resources.res`**: nueva entrada `SPRITE mapShip "sprite/map_ship.png" 1 1 BEST`
  (1x1 tile, a diferencia de las 2x2 de `playerShip`/`enemyShip`).
  Rescomp confirma en el log de build que su paleta es idéntica a la
  de `playerShip` (mismos 2 colores) — no hace falta cargarla aparte,
  comparte `PAL1`.
- **`main.c`**: nuevo `Sprite *mapShipSprite`, creado junto a
  `playerSprite` con la misma `PAL1`. Al abrir el mapa, en vez de
  reposicionar `playerSprite` (como hacía el §22), ahora se **oculta**
  `playerSprite` (igual que los enemigos) y se posiciona+muestra
  `mapShipSprite`, centrado en la caja de 24x16px con offset `+8,+4`
  (`(24-8)/2`, `(16-8)/2`). El parpadeo y el recoloreado a blanco
  (§23, `PLAYER_SHIP_INK_INDEX`) pasan a operar sobre `mapShipSprite`
  en vez de `playerSprite` — como comparten `PAL1`, la misma constante
  de índice de color sigue siendo válida sin cambios. Al cerrar el
  mapa, `mapShipSprite` se oculta y `playerSprite` vuelve a mostrarse
  (su posición se restaura sola, como ya pasaba en el §22).
- Verificado en BlastEm: build limpio (`make`, sin warnings — el log
  de rescomp confirma la paleta compartida), arranque limpio sin
  errores en el log tras el rebuild.

## 25. Enemigos: patrón simple arriba/abajo o izquierda/derecha, rebote en colisión

Petición del usuario: sustituir el patrón "recto por defecto, gira si
choca" del §12 (derecha → izquierda → reversa, elegido tras varias
rondas de fuzzing sobre el problema del bucle cerrado) por algo mucho
más simple — cada enemigo se mueve solo en un eje (vertical u
horizontal) y rebota (invierte) al chocar, sin girar nunca a un eje
perpendicular.

Esto resulta ser exactamente el mismo patrón de colisión que ya usa
`movePlayer()` en `player.c` para el caso de "seguir recto": probar el
siguiente píxel en la dirección actual, si choca invertir la
dirección. Al no haber ninguna decisión de giro entre varias
direcciones, desaparece de raíz toda la familia de problemas de
"bucle cerrado atrapa al enemigo" que motivó el diseño del §12 — ya
no hay nada en lo que quedarse atrapado.

Implementación (`enemy.c`):
- Se elimina toda la lógica de giro: `tileBlockedInDir`, `turnRight`,
  `turnLeft`, y la comprobación de alineación a tile
  (`x % MAZE_TILE_PX == 0`) que antes gateaba cuándo re-decidir. Ya no
  hace falta decidir solo en los límites de celda porque ya no hay
  una decisión de "girar" que tomar — solo "¿choco? invierto".
- `Enemy_update` pasa a tener la misma forma que `movePlayer()`
  (player.c): un `switch` por dirección, con colisión a nivel de
  píxel usando el mismo hitbox con inset de 2px ("TILE-2", evita
  falsos positivos de rozamiento en los bordes de celda) —
  `collideUp/Down/Left/Right`, nuevas, calcadas de las de player.c.
  Al chocar, invierte con el opuesto exacto en el mismo eje
  (`DIR_UP↔DIR_DOWN`, `DIR_LEFT↔DIR_RIGHT`) en vez de
  `Enemy_opposite()` genérico — aunque en la práctica son
  equivalentes, se escribe explícitamente para dejar claro que nunca
  cruza de eje.
- `Enemy_spawnForRoom` no cambia: `e->dir = roomSeed & 3` ya elegía
  una de las 4 direcciones, y ahora esa elección inicial fija el eje
  del enemigo para toda su estancia en la sala (nunca cambia después).
  Los dos enemigos de una sala pueden compartir eje o no, según lo que
  toque — no se ha forzado variedad entre ellos, no se pidió.
- El rebote enemigo-enemigo (`Enemy_opposite`, ya existente en
  `main.c`) no cambia — sigue invirtiendo ambos al solaparse,
  independientemente del eje de cada uno.
- **Verificado en el host** (`test_enemy_axis.c`, nuevo): 1500
  semillas × 16 combinaciones de puertas × 3000 frames cada una
  (24000 ejecuciones) — el eje de cada enemigo nunca cambia una vez
  fijado en el spawn, y su hitbox (con el mismo inset de 2px que usa
  el propio juego) nunca solapa un muro ni el anillo exterior en
  ningún frame. Primera pasada del test dio 24000/24000 fallos por un
  bug en el propio test (comprobaba el sprite visual de 16x16 completo
  en vez del hitbox real de colisión, inset 2px) — corregido el test,
  no el código del juego, y confirmado 0 fallos tras el arreglo.
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 26. Lectura de FPS en pantalla (debug)

Petición del usuario: mostrar los FPS en pantalla como información de
debug.

Implementación (`main.c`): `SYS_getFPS()` (SGDK, cuenta cuántas veces
se ha llamado en el último segundo — su propio comentario pide
llamarla exactamente una vez por frame) se lee una vez por iteración
del bucle principal, incondicionalmente respecto a `gameState`, justo
antes de `SYS_doVBlankProcess()`. Se dibuja como texto (`"FPS60"`
estilo) en la esquina superior derecha (fila 0, columna calculada
para pegar a la derecha) sobre `BG_B` con prioridad alta
(`VDP_setTextPriority(1)`, mismo truco que ya usa `Items_drawHud`
para el HUD de letras) — visible por encima de los tiles de baja
prioridad de `BG_A` (maze, menú, mapa) sin que ninguno de esos sitios
necesite saber de esto. Fila 0 en vez de la fila 1 del HUD de items
para no solaparse con él.
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 27. Habitación de inserción: punto de partida real de la nave

Petición del usuario: la nave aparece primero en una habitación con un
único tipo de muro (un patrón de dithering fijo, no los 9 aleatorios
de siempre), con 1 sola salida que da a una habitación periférica
cualquiera del mapa (cualquiera de las salas en el borde de la
rejilla) — "la habitación de inserción donde empieza el mundo".

Decisiones confirmadas (preguntadas antes de implementar):
- **Alcance**: sala nueva y especial, fuera de la rejilla del mapa —
  no forma parte del árbol de Prim's, `startCol`/`startRow` (raíz
  lógica del árbol para BFS/bloqueos/secciones) no cambia de
  significado ni de valor.
- **Textura**: un único patrón de dithering fijo (no un tile nuevo),
  reutilizando el arte existente.
- **Destino**: una habitación periférica cualquiera (borde de la
  rejilla), elegida al azar en la generación de cada partida.

Implementación:
- **`maze.c`**: nueva `Maze_generateInsertionRoom(roomSeed)`, hermana
  de `Maze_generateRoom` — reutiliza el mismo `carve()`/
  `bridgeToSeed()` (ambos ya estáticos en el archivo), pero rellena
  la rejilla entera con un único valor de celda fijo
  (`INSERT_WALL_VARIANT`) en vez de llamar a `randomWallVariant()` —
  funciona sin tocar `carve()` en absoluto, porque tanto esa función
  como `bridgeToSeed()` solo distinguen `PATH` (0, ya tallado) de
  "cualquier otro valor" (muro), nunca miran CUÁL valor de muro es.
  Puerta única, siempre en el borde sur (arbitrario, sin significado
  espacial ya que esta sala no pertenece a la rejilla). No usa
  `wallHueBase`/color por sección (spec §18 no aplica aquí).
- **`guidemap.c`**: nueva `selectInsertionLink()`, llamada dentro de
  `GuideMap_generate()` justo después de `pruneLeaves()` — recopila
  todas las `CELL_ROOM` cuya fila/columna coincide con el borde de la
  rejilla activa (`row==0`, `row==mapRows-1`, `col==0` o
  `col==mapCols-1`) y elige una al azar, guardada en
  `insertLinkCol`/`insertLinkRow` (nuevas variables `extern`, mismo
  patrón que `startCol`/`goalCol`). Reutiliza `frontier[]` como
  scratch, igual que `selectGoal`/`selectItemRooms` más abajo en la
  misma función.
- **`main.c`**: nuevo estado `inInsertRoom`. `newGame()` genera la
  sala de inserción (`Maze_generateInsertionRoom(mapSeed)`) en vez de
  cargar `(startCol,startRow)` directamente; la nave aparece en su
  punto central (mismo `Player_spawnAtRoomCenter` de siempre, siempre
  camino garantizado); los enemigos permanecen ocultos (esta sala no
  tiene enemigos). El botón C queda deshabilitado mientras
  `inInsertRoom` (no hay nada que previsualizar antes de entrar al
  mapa real, y `currentCol`/`currentRow` aún no son válidos). El
  bucle principal de movimiento se divide en tres ramas
  (`mapViewOpen` / `inInsertRoom` / normal): mientras está en la sala
  de inserción, `Player_updateRoom` se llama con solo el sur abierto;
  al cruzarlo (`EXIT_SOUTH`), la transición es tipo teletransporte —
  no hay una apertura física correspondiente en el muro de la sala
  periférica de destino (`Maze_generateInsertionRoom` documenta esto
  explícitamente) — se fija `currentCol/currentRow = insertLinkCol/
  insertLinkRow`, se llama a `loadRoom()` con normalidad (genera esa
  sala exactamente como si se hubiera llegado por el árbol, con sus
  puertas/bloqueos/sección propios sin ningún cambio) y la nave
  aterriza en el propio punto central de esa sala
  (`Player_spawnAtRoomCenter`, garantizado abierto siempre, evita
  cualquier riesgo de aparecer dentro de un muro). Los enemigos de
  esa sala se hacen visibles en ese momento (ya generados por
  `loadRoom`). No hay camino de vuelta a la sala de inserción — es de
  un solo uso, coherente con "donde empieza el mundo".
- **Nota de diseño explorada y descartada**: se consideró marcar la
  puerta de conexión también como un bit de puerta real en
  `guideMap` (para que la sala periférica tuviera una apertura física
  visible hacia la sala de inserción), pero se descartó: el BFS/
  bloqueos/secciones de `guidemap.c` asumen que todo bit de puerta
  corresponde a un vecino real dentro de la rejilla — un bit de
  puerta hacia una celda fuera de rango (fila/columna -1, al norte de
  la fila 0 por ejemplo) causaría lecturas fuera de límites en
  `bfsFromRoom`. El teletransporte evita este riesgo por completo sin
  perder nada del comportamiento pedido.
- **Verificado en el host** (`test_insertion.c`, nuevo, 6000
  generaciones entre semillas 1-2000 × 3 tamaños de preset):
  `insertLinkCol/Row` siempre cae dentro de los límites, siempre es
  una `CELL_ROOM` real, y siempre está en el perímetro de la rejilla
  activa — 0 fallos. Por separado, `Maze_generateInsertionRoom`
  probada con 6000 semillas propias: el punto de spawn central
  siempre está libre de muro, exactamente 2 celdas del borde sur
  quedan abiertas (el tramo de la puerta) y absolutamente ningún otro
  borde (norte/este/oeste, y el resto del sur) queda abierto — 0
  fallos. Se reutilizó el mismo hitbox con inset de 2px ("TILE-2") que
  usa la colisión real del juego, igual que en `test_enemy_axis.c`
  (spec §25).
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 28. Combo de reset: A+B+C+ARRIBA vuelve al menú

Petición del usuario: pulsando A+B+C+ARRIBA a la vez se resetea la
partida.

Decisiones confirmadas (preguntadas antes de implementar): el reset
vuelve al menú de selección de tamaño (no arranca una partida nueva
directamente — hay que pulsar START de nuevo, igual que al inicio), y
el combo funciona desde cualquier pantalla, no solo durante la
partida.

Implementación (`main.c`):
- `RESET_COMBO` (`BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_UP`) se
  comprueba al principio del bucle principal, ANTES de la rama
  `gameState == STATE_MENU` / `STATE_PLAYING` (como un tercer `if`
  que tiene prioridad) — así siempre gana ese frame sobre lo que
  cualquiera de esos 4 botones haría normalmente (ninguno tenía uso
  individual salvo C, que solo se pierde ese frame concreto, sin
  efecto visible ya que el reset lo sustituye de todas formas).
  Disparo por flanco (`(state & COMBO) == COMBO` Y no lo era en
  `prevState`), no se repite mientras se mantienen pulsados.
- Nueva `resetToMenu()`: pone `gameState = STATE_MENU`, limpia
  `mapViewOpen`/`inInsertRoom` (sin efecto real ya que `newGame()` los
  vuelve a fijar al arrancar una partida nueva, pero evita dejar
  estado a medias visible entretanto), oculta los tres sprites que
  podrían estar visibles (`playerSprite`, `mapShipSprite`,
  `enemySprites[]` — de lo contrario se verían "congelados" sobre la
  pantalla de menú) y llama a `drawMenu()`.
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 29. Puerta real (no teletransporte) entre la sala de inserción y su destino

Feedback del usuario sobre el §27: quería que el aterrizaje coincidiera
con una entrada real de la habitación periférica de destino, no con el
centro de la sala vía teletransporte (que era la solución adoptada
entonces precisamente para evitar el riesgo de romper el BFS/bloqueos
de `guidemap.c` — ver la "nota de diseño descartada" del §27).

Diseño revisado: en vez de escribir el bit de puerta extra dentro de
`guideMap` (el riesgo real: BFS, bloqueos y secciones asumen que todo
bit de puerta corresponde a un vecino real en la rejilla, y leer un
vecino fuera de rango causaría lecturas fuera de límites), la
dirección de enlace se mantiene como un canal aparte
(`insertLinkDir`), y se fusiona con las puertas reales del árbol
**solo en el momento de generar/leer esa sala concreta** — nunca
dentro de `guideMap` mismo. Como esa dirección, por construcción,
siempre cae en un lado sin vecino real (fila 0 → norte, última fila →
sur, columna 0 → oeste, última columna → este), nunca puede coincidir
con una puerta real del árbol para esa sala — no hay ambigüedad
posible entre "puerta de árbol" y "puerta de inserción".

Implementación:
- **`guidemap.c`**: `selectInsertionLink()` ahora recopila candidatos
  como tripletas (sala, lado libre) en vez de solo (sala) — una sala
  de esquina ofrece 2 candidatos (uno por lado sin vecino), una de
  borde normal solo 1. Se elige una tripleta al azar; el resultado
  (`insertLinkCol`, `insertLinkRow`, nueva `insertLinkDir`) se expone
  igual que antes vía `extern` en `guidemap.h`.
- **`main.c`**, `loadRoom(col,row)`: cuando `(col,row)` coincide con
  `(insertLinkCol,insertLinkRow)`, las variables locales `doorN/E/S/W`
  que se pasan a `Maze_generateRoom` fusionan (`||`) el bit real del
  árbol (`cell.doorN` etc., de `guideMap`) con `insertLinkDir` — el
  bloqueo (`lockedN` etc.) sigue calculándose solo sobre `cell.doorN`
  (el bit crudo), nunca sobre el fusionado, así que la puerta de
  inserción nunca puede quedar bloqueada por el sistema de letras
  (§16) — no forma parte de ese grafo en absoluto. `Maze_generateRoom`
  en sí no cambió nada: simplemente recibe un `TRUE` más en la
  combinación de puertas, algo que ya soportaba (spec §12/§25, 16
  combinaciones de puertas ya probadas por fuzzing).
- **`main.c`**, transición de salida (sala de inserción → periférica):
  en vez de `Player_spawnAtRoomCenter`, se usa la nueva
  `positionPlayerEnteringViaDoorDir(insertLinkDir)` — coloca a la nave
  justo dentro del borde real recién abierto, alineada con el tramo de
  la puerta (misma tabla de posicionamiento que ya usa
  `enterRoomFrom`, solo indexada por "lado de ENTRADA de la sala
  nueva" en vez de "lado de SALIDA de la sala vieja" — son
  exactamente los mismos 4 casos, solo mirados desde el otro extremo
  de la puerta).
- **`main.c`**, camino de vuelta (nuevo, no existía en el §27): en la
  rama normal de movimiento, `doorN/E/S/W` que se pasan a
  `Player_updateRoom` también fusionan `insertLinkDir` cuando
  `(currentCol,currentRow) == (insertLinkCol,insertLinkRow)`. Si el
  `exitDir` resultante coincide exactamente con esa dirección
  (`exitDirForDoorDir(insertLinkDir)`, nueva función que traduce
  `DOOR_N/E/S/W` de guidemap.h al `EXIT_NORTH/EAST/SOUTH/WEST` de
  player.h — enumeraciones distintas para las mismas 4 direcciones),
  la nave vuelve a la sala de inserción (regenerada, siempre el mismo
  seed `mapSeed`) en vez de llamar a `enterRoomFrom` — entra por su
  única puerta (sur), con el mismo posicionamiento que usa su propia
  llegada. Cualquier otro `exitDir` (una puerta real del árbol) sigue
  el camino normal de `enterRoomFrom`, sin cambios. El pickup de
  ítems y la actualización de enemigos de ese frame se saltan
  explícitamente cuando esto ocurre (`if (!inInsertRoom) { ... }`)
  para no operar con un `(currentCol,currentRow)` obsoleto (sigue
  apuntando a la sala periférica que se acaba de abandonar) contra la
  posición ya recién movida a la sala de inserción.
- **Verificado en el host** (`test_insertion.c`, ampliado): además de
  las comprobaciones ya existentes (destino válido, en el perímetro,
  puerta única de la sala de inserción), ahora también se verifica
  que `insertLinkDir` corresponde estructuralmente a un lado
  genuinamente sin vecino de rejilla (no solo "ahora mismo no hay
  puerta ahí", sino "no podría haberla nunca", comprobado por
  aritmética de fila/columna independiente del código de producción),
  y que nunca coincide con una puerta real ya existente de esa sala
  (`GuideMap_hasDoor`) — 6000 generaciones, 0 fallos. Re-ejecutados
  `test_locks.c`, `test_sections.c`, `test_overlay_smoke.c` y
  `test_enemy_axis.c` sin fallos (ninguno de sus supuestos se vio
  afectado, ya que `guideMap` en sí nunca se toca).
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 29bis. Bug real: la sala periférica de destino a veces quedaba bloqueada

Bug reportado por el usuario tras probar el §29: a veces la nave entra
en una sala periférica "sin salida (tapada)". Diagnóstico confirmado:
`selectInsertionLink()` elegía la sala de destino entre **todas** las
salas del perímetro, sin ningún filtro relacionado con el sistema de
bloqueos (§16). Como el desbloqueo es monótono pero empieza siendo muy
restrictivo (solo el camino hacia la letra A está desbloqueado al
principio de la partida), una sala periférica elegida al azar casi
nunca cae exactamente en ese camino — y el mecanismo de bloqueo, por
diseño, sella la puerta de esa sala hacia su propio padre en el árbol
**salvo que el padre esté también en el camino desbloqueado**. Si ni
la sala ni su padre están en el camino a A, su única puerta real
(hacia el padre) queda sellada — la nave queda atrapada, sin más
salida que volver a la sala de inserción (y, al volver a salir por la
misma puerta fija de esa sala, cae otra vez en la misma sala sellada:
un bucle sin fin entre las dos).

Esto invalida una asunción incorrecta hecha durante el diseño del
§27: se había razonado que la puerta de una sala hacia su padre "casi
nunca" queda bloqueada porque el padre "casi nunca" está bloqueado —
cierto solo para salas que están en (o cerca) del camino al ítem
actual, pero falso en general, ya que el bloqueo se propaga hacia
FUERA desde el inicio siguiendo exactamente ese único camino, y
cualquier sala fuera de él (la inmensa mayoría del árbol al principio
de la partida) tiene un padre que también está fuera — sellada por
partida doble.

Corrección: `selectInsertionLink()` ahora solo elige entre salas que
están en el camino real (único, por ser un árbol) desde el inicio
hasta la sala de la letra A — el único conjunto de salas garantizado
desbloqueado desde el primer instante y **para siempre** (el
desbloqueo es monótono: una vez que un ítem cuenta como "debido", su
camino nunca vuelve a bloquearse, así que restringir aquí no solo
arregla el arranque sino toda la partida).

Implementación (`guidemap.c`):
- `GuideMap_generate()` reordena sus pasos: `selectItemRooms()` (fija
  `itemCol[]/itemRow[]`) pasa a ejecutarse ANTES de
  `selectInsertionLink()`, que ahora depende de conocer dónde está la
  letra A.
- Nueva `markPathToFirstItem()`: reconstruye el camino único (spec
  §4, es un árbol) desde `(startCol,startRow)` hasta
  `(itemCol[0],itemRow[0])`, caminando hacia atrás desde la sala de A
  y dando siempre el paso hacia el vecino cuya distancia BFS
  (`bfsFromStart()`, reutilizada) sea exactamente una unidad menor —
  misma técnica que usaba el extinto `tracePathTo()` del §15 (mapa
  "solo la ruta", revertido entonces, resucitada aquí para un
  propósito distinto). Resultado guardado en `pathToAScratch[][]`,
  scratch nuevo del tamaño de la rejilla.
- `selectInsertionLink()` reescrita en dos niveles, ambos restringidos
  a `pathToAScratch`:
  1. **Nivel 1** (preferido): salas del camino a A cuyo lado libre
     coincide además con el borde real de la rejilla — mantiene el
     aspecto "llega desde fuera del mapa" del §27 siempre que sea
     posible.
  2. **Nivel 2** (reserva): cualquier sala del camino a A con
     cualquier lado sin puerta de árbol, sin exigir que sea borde de
     rejilla — cubre mapas donde el camino a A nunca toca el
     perímetro exterior. Ese lado "libre" puede coincidir con una
     celda vecina que SÍ es una sala real (solo que Prim's no la
     conectó por ahí, llegó por otra rama) — no supone ningún
     problema: `main.c` nunca intenta cargar esa vecina a través de
     esta puerta, solo la usa para el enlace especial de vuelta a la
     sala de inserción.
  Reserva final defensiva idéntica a la del §27/§29 si ambos niveles
  quedan vacíos (inalcanzable en la práctica).
- **Verificado en el host** (`test_insertion.c`, reescrito): además
  de las comprobaciones ya existentes, se añadió un trazador de
  camino independiente (`independentPathTo`, no reutiliza el código
  interno de `guidemap.c`) que confirma que `insertLinkCol/Row` está
  siempre en el camino a A; y, la comprobación central de este bug,
  se simula recoger las 5 letras en orden llamando a
  `GuideMap_recomputeLocks()` tras cada una y comprobando
  `GuideMap_isRoomLocked(insertLinkCol,insertLinkRow)` — debe dar
  `FALSE` siempre, en cada uno de los 5 pasos, no solo al principio.
  5984 generaciones válidas, 0 fallos (antes de la corrección, la
  misma comprobación fallaba en el primer paso — `nextIndex=0` —
  confirmando el bug exactamente como lo describió el usuario).
  Re-ejecutados `test_locks.c`, `test_sections.c` y
  `test_overlay_smoke.c` sin fallos.
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 29ter. La puerta de la propia sala de inserción también varía

Feedback del usuario: la puerta de la sala de inserción siempre
aparecía en el mismo sitio (borde sur fijo, spec §27/§29), mientras
que el lado de llegada a la sala periférica sí variaba cada partida
(`insertLinkDir`, spec §29) — pidió que la salida de la sala de
inserción también variase.

Implementación:
- **`maze.c`/`.h`**: `Maze_generateInsertionRoom` gana un parámetro
  `doorDir` (spec §29ter) que elige cuál de los 4 anchors
  (`ANCHOR_N/E/S/W`, ya existentes, compartidos con
  `Maze_generateRoom`) usar tanto para el objetivo forzado de
  `carve()` como para el punzonado final del borde — antes tenía
  ambos hardcodeados al sur. Como `maze.c` no incluye `guidemap.h`
  (para no crear una dependencia cruzada entre módulos — el mismo
  motivo por el que `Maze_generateRoom` ya recibe 4 bools sueltos en
  vez del enum), se definen constantes `INSERT_DOOR_N/E/S/W` locales
  con los mismos valores numéricos 0/1/2/3 que `DOOR_N/E/S/W` de
  `guidemap.h` — documentado explícitamente en el comentario de la
  declaración, mismo patrón que ya usa `exitDirForDoorDir` en
  `main.c` para traducir entre las dos enumeraciones de dirección del
  proyecto.
- **`main.c`**: nueva `insertRoomDoorDir`, elegida una vez por
  partida en `newGame()` (`random() & 3`, mismo patrón de bitmask que
  ya usa `enemy.c` para elegir dirección inicial) y reutilizada sin
  cambios durante el resto de esa partida — tanto al generar la sala
  por primera vez como en cualquier viaje de vuelta posterior (spec
  §29). Las dos comprobaciones que antes asumían `EXIT_SOUTH`/`DOOR_S`
  fijos (la salida inicial hacia la periférica, y el regreso desde la
  periférica) ahora comparan contra `exitDirForDoorDir(insertRoomDoorDir)`
  y pasan las 4 puertas `(insertRoomDoorDir==DOOR_N)` etc. a
  `Player_updateRoom`/`positionPlayerEnteringViaDoorDir`, igual que ya
  se hacía con `insertLinkDir` para el otro extremo del enlace.
- **Verificado en el host** (`test_insertion.c`, ampliado): se
  recompiló `Maze_generateInsertionRoom` con su nueva firma y se
  probaron las 4 direcciones para cada semilla/tamaño (antes solo
  probaba la fija) — para cada una, exactamente el borde solicitado
  queda con un tramo de 2 celdas abierto y los otros tres permanecen
  completamente sólidos, y el punto de spawn central sigue siempre
  libre de muro — 5984 semillas × 4 direcciones, 0 fallos.
  Re-ejecutados `test_locks.c`, `test_sections.c`,
  `test_overlay_smoke.c` y `test_enemy_axis.c` sin fallos.
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 29quat. La salida de la sala de inserción coincide con la opuesta de la de llegada

Petición del usuario: la coordenada/lado de la salida de la sala de
inserción tiene que coincidir con el **opuesto** del lado de entrada
de la sala periférica — sur↔norte, este↔oeste — en vez de elegirse al
azar de forma independiente (como quedó tras el §29ter).

Implementación (`main.c`): `insertRoomDoorDir` deja de sortearse con
`random() & 3` y pasa a derivarse directamente de `insertLinkDir`
(ya fijado por `GuideMap_generate()`, ejecutado justo antes en
`newGame()`) con `(insertLinkDir + 2) & 3` — la misma transformación
"+2 mod 4" que ya usan `guidemap.c`'s propio `opposite()` (estático,
usado al emparejar puertas al tallar el árbol) y `Enemy_opposite()`
en `enemy.c`, para la misma pareja de 4 direcciones
(DOOR_N=0↔DOOR_S=2, DOOR_E=1↔DOOR_W=3). El resultado: salir por el
sur de la sala de inserción ahora siempre lleva a entrar por el norte
de la periférica (y así con las otras 3 combinaciones), leyéndose
como una línea recta continua en vez de dos puertas orientadas al
azar sin relación entre sí.
- Verificado: la fórmula se comprobó de forma aislada contra las 4
  parejas opuestas (N↔S, E↔S únicamente ida y vuelta correctas) antes
  de aplicarla — coincide exactamente. Es un cálculo puro sobre un
  valor que `guidemap.c` ya genera y valida (`insertLinkDir`, spec
  §29bis/§29ter), así que los tests existentes de generación de mapa
  (`test_insertion.c`, `test_locks.c`, `test_sections.c`) no
  necesitaban cambios y se re-ejecutaron para confirmar que nada se
  rompió — 0 fallos.
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 30. Puertas en cualquier posición del borde, no solo en el centro

Petición del usuario: las entradas y salidas de las habitaciones ya no
tienen que estar necesariamente en el medio del borde — pueden estar
en cualquier coordenada válida, siempre que la entrada de una sala
coincida exactamente con la salida de la contigua (misma columna para
puertas norte/sur, misma fila para este/oeste).

Decisiones confirmadas (preguntadas antes de implementar):
- Cada puerta de una misma sala (p.ej. norte Y sur a la vez) elige su
  posición de forma totalmente independiente — no comparten columna/
  fila entre sí.
- Cualquier posición válida del borde vale (no un rango acotado cerca
  del centro), respetando un margen mínimo para no invadir las
  esquinas.
- También aplica a la conexión de la sala de inserción con la
  periférica (spec §27-§29quat).

Implementación:
- **`guidemap.h`**: `MapCell` gana 4 campos nuevos,
  `doorOffsetN/E/S/W` — la columna (N/S) o fila (E/W) donde esa puerta
  concreta se sitúa. Solo tienen sentido cuando el bit `doorX`
  correspondiente es `TRUE`. `insertLinkOffset` (nuevo `extern`,
  junto a `insertLinkCol/Row/Dir`) hace lo mismo para el enlace de la
  sala de inserción.
- **`guidemap.c`**: `openDoor()` (llamada por `carveTree()` al tallar
  cada arista del árbol) sortea ahora un offset — columna en rango
  `[DOOR_COL_MIN=2, DOOR_COL_MAX=MAZE_W-4=16]` para puertas N/S, fila
  en `[DOOR_ROW_MIN=2, DOOR_ROW_MAX=MAZE_H-4=10]` para E/W — y lo
  escribe en AMBOS lados de la arista a la vez (mismo valor, vía el
  nuevo `setDoorOffset()`), garantizando que ambas salas coincidan
  exactamente sin necesitar traducir nada entre ellas. Nuevo
  `GuideMap_doorOffset(col,row,dir)` expone el valor a otros módulos.
  `selectInsertionLink()` sortea `insertLinkOffset` de la misma forma,
  en el rango que corresponda según el eje de `insertLinkDir`.
  **Nota**: esto añade llamadas a `random()` dentro de `carveTree()`
  que no existían antes, así que una misma semilla ahora genera un
  mapa con forma distinta a como lo hacía antes de este cambio — sin
  consecuencias prácticas, ya que `mapSeed` no es nunca expuesto ni
  persistido al jugador (siempre `random()` fresco en cada
  `newGame()`).
- **`maze.c`/`.h`**: `Maze_generateRoom` cambia su parámetro
  `sectionHue` a venir acompañado de un nuevo `const u8 doorOffsets[4]`
  (indexado con la misma convención 0/1/2/3 = N/E/S/W de
  `guidemap.h`, sin necesitar incluir ese header aquí — mismo patrón
  ya usado en `Maze_generateInsertionRoom`). Los antiguos
  `ANCHOR_N_X/Y` etc. (constantes fijas) se sustituyen por
  `anchorForDoor(dir, offset, &x, &y)`, que combina la profundidad
  fija de cada lado (`ANCHOR_DEPTH_N/E/S/W` — antes parte de los
  `ANCHOR_*`, ahora aislada) con el offset variable. El propio
  `MAZE_DOOR_COL`/`MAZE_DOOR_ROW` no cambia de valor ni de rol — sigue
  siendo el centro/semilla de la sala (de ahí cuelgan el spawn del
  jugador, la posición fija de los ítems, y el punto desde el que
  arranca `carve()`), simplemente ya no marca dónde están las
  puertas. `Maze_generateInsertionRoom` gana un parámetro
  `doorOffset` con el mismo significado.
- **`player.h`/`.c`**: `Player_updateRoom` gana 4 parámetros
  `doorOffsetN/E/S/W`; `inDoorSpan()` los usa en vez de
  `MAZE_DOOR_COL`/`MAZE_DOOR_ROW` fijos.
- **`main.c`**: nueva `doorOffsetFor(col,row,dir)` — capa fina que
  también sabe devolver `insertLinkOffset` cuando `(col,row,dir)`
  coincide con el enlace de inserción (que no es una puerta de árbol
  real, así que `GuideMap_doorOffset` no tendría un valor válido para
  ella). `loadRoom()` la usa para construir el array `doorOffsets[4]`
  que pasa a `Maze_generateRoom`. `positionPlayerEnteringViaDoorDir`
  gana un parámetro `offset` (coordenada real en vez de
  `MAZE_DOOR_COL`/`ROW`); `enterRoomFrom()` se simplifica
  reutilizándola en vez de repetir su propia tabla de posicionamiento
  (antes duplicada entre ambas funciones). Todas las llamadas a
  `Player_updateRoom` (sala de inserción, sala enlazada, y el bucle de
  movimiento normal) pasan los 4 offsets correspondientes.

## 30bis. Bug de conectividad encontrado por fuzzing: puente diagonal roto

`test_door_offsets.c` (nuevo, verifica con BFS de accesibilidad
independiente que cada puerta activa es alcanzable desde la semilla
de talla de la sala) encontró un fallo real en ~3.8% de las
combinaciones (1227 de 32000): la puerta quedaba físicamente pintada
como suelo pero completamente aislada del resto de la sala.

Causa: `bridgeToSeed(x,y)` (el mecanismo de emergencia que conecta un
"ancla" de puerta con la semilla de la sala cuando `carve()` no llega
a tallarlo de forma natural, spec §11 Paso3) movía **los dos ejes a
la vez** en cada paso cuando ambos aún diferían de la semilla —
produciendo una escalera diagonal donde cada celda solo tocaba a la
siguiente por una **esquina**, no por un lado completo. Esto era
invisible antes del §30 porque todas las anclas antiguas
(`ANCHOR_N_X`, etc.) compartían siempre un eje exacto con la semilla
(`ROOM_SEED_COL`/`ROOM_SEED_ROW`), así que en la práctica el bucle
nunca movía más de un eje a la vez — la línea resultante siempre era
recta (horizontal o vertical), nunca diagonal. Con offsets
independientes por puerta (spec §30), un ancla casi nunca comparte
eje con la semilla, así que el camino diagonal se volvió el caso
común, no la excepción — y la nave, al moverse solo en las 4
direcciones cardinales, nunca podía cruzar esa unión de solo-esquina.

Corrección: `bridgeToSeed` ahora mueve un eje por vez — primero cierra
la distancia en X (marcando cada celda intermedia de esa fila),
después la distancia en Y (marcando cada celda intermedia de esa
columna) — un camino en "L" donde cada par de celdas consecutivas
comparte un lado completo, nunca solo una esquina.
- **Verificado en el host** (`test_door_offsets.c`): Parte 1 — 32000
  combinaciones (2000 semillas × 16 combinaciones de puertas), cada
  puerta activa con un offset independiente y variado por dirección;
  se confirma que el tramo de la puerta nunca es muro y siempre es
  alcanzable (BFS propio, no reutiliza el código interno de
  `maze.c`) desde la semilla de la sala — 0 fallos tras la
  corrección (1227 fallos antes). Parte 2 — 3000 generaciones de mapa
  completo (1000 semillas × 3 tamaños): para cada arista real del
  árbol, el offset coincide exactamente en ambos lados y cae dentro
  del rango válido de su eje — 0 fallos. `test_insertion.c` ampliado
  para verificar que la apertura de la sala de inserción cae
  exactamente en el offset pedido (no solo "en algún punto del
  borde"). `test_enemy_axis.c` ampliado para generar sus 16
  combinaciones de puertas con offsets variados en vez de siempre
  centrados. Re-ejecutados `test_locks.c`, `test_sections.c` y
  `test_overlay_smoke.c` sin fallos (lógica de `guidemap.c` no
  relacionada con esto, sin cambios).
- Verificado en BlastEm: build limpio (`make`, sin warnings), arranque
  limpio sin errores en el log tras el rebuild.

## 31. Menú de inicio: sistema solar animado

Petición del usuario: sustituir el menú de texto por una pantalla con
varios planetas girando alrededor de un sol, con las líneas de su
órbita visibles, y una flecha/cursor justo encima del planeta
seleccionado. Botón A confirma y empieza la partida.

Decisión de mapeo (no requería preguntar, era la única lectura
razonable dado que el menú solo elegía tamaño de mapa hasta ahora):
cada planeta = uno de los 3 `sizePresets[]` existentes (6x4/8x6/10x8),
mismo orden — órbita interior = mapa más pequeño. `LEFT`/`RIGHT` sigue
cambiando `sizePresetIndex` exactamente igual que antes; solo cambió
la representación visual y el botón de confirmación (`A` en vez de
`START`, tal como pidió el usuario).

Implementación:
- **Módulo nuevo `menu.c`/`.h`**: separado de `main.c` para mantener
  la responsabilidad de la animación/render del sistema solar aislada,
  siguiendo el mismo patrón modular que `maze.c`/`guidemap.c`/etc.
  Expone `Menu_loadGraphics()` (una vez, boot), `Menu_draw()` (arte
  estático: sol + líneas de órbita, sobre `BG_A`), `Menu_setVisible(bool)`
  (mostrar/ocultar los 4 sprites del menú y reiniciar los ángulos de
  órbita al entrar), `Menu_update(u8 selectedIndex)` (cada frame:
  avanza cada órbita, reposiciona los 3 planetas y el cursor).
- **Matemática de órbita**: usa `F16_computePositionEx` (SGDK,
  `maths.h`, confirmada su fórmula exacta leyendo `maths.c`:
  `x2 = x1 + dist·cos(áng)·cosMul`, `y2 = y1 + dist·sin(áng)·sinMul`) —
  con `dist=FIX16(1)` y `cosMul`/`sinMul` = radio X/Y de cada órbita,
  da directamente una posición elíptica (no circular, para aprovechar
  mejor la pantalla 320x224) sin necesitar llamar a seno/coseno a
  mano. Cada planeta tiene su propio ángulo (`orbitAngle[3]`,
  persistente entre frames) y velocidad angular fija
  (interior más rápido, como un sistema solar real) — puramente
  decorativo, sin significado de jugabilidad.
- **Líneas de órbita**: en vez de un dibujo pixel-perfecto (no hay
  motor de líneas en este proyecto), se recorre cada elipse en pasos
  de `ORBIT_DOT_STEP_DEG=4` grados, convirtiendo cada punto muestreado
  a coordenadas de tile (8px) y colocando ahí un tile "punto" — a esta
  resolución de muestreo, tiles consecutivos quedan iguales o
  contiguos, leyéndose como un anillo punteado continuo. El sol usa la
  misma técnica que ya usaba `guidemap.c` para el relleno sólido de la
  sala actual (spec §18/§23): recorrer una caja de tiles y comprobar
  `dx²+dy² <= radio²` contra el centro de cada tile.
- **Arte nuevo** (`res/sprite/`, todo duotono `#252525`/`#987DFA`,
  mismo estilo que el resto del juego): `menu_tiles.png` (16x8, 2
  celdas: punto de órbita + relleno de sol, plano `BG_A`, reutiliza
  `PAL0` ya cargada por `Maze_loadGraphics`); `planet_small.png` (8x8),
  `planet_medium.png` (16x16), `planet_large.png` (24x24) — el tamaño
  del planeta crece con el tamaño del mapa que representa, una pista
  visual gratuita que sustituye parte de lo que antes solo decía el
  texto; `cursor_arrow.png` (8x8, triángulo apuntando hacia abajo con
  un pequeño "rabo", para que se lea claramente como cursor). Los 4
  sprites nuevos usan `PAL3`, la única paleta que no usaba nada más en
  todo el juego (`PAL0`=maze/texto, `PAL1`=nave/mapShip,
  `PAL2`=enemigos) — así el menú nunca pisa ningún color en uso.
- **VRAM**: `menuTiles` se carga justo después del tileset de
  `guidemap.c`, apilando tiles igual que `guidemap.c` ya hacía tras
  `maze.c`. Como `MAP_TILE_BASE` (guidemap.c) es privado, se expuso un
  nuevo `GUIDEMAP_TILE_COUNT=10` público en `guidemap.h` (mismo rol
  que `MAZE_TILE_COUNT` de `maze.h`) para que `menu.c` pueda calcular
  su propia base sin necesitar acceso a nada privado de `guidemap.c`.
- **Cursor "justo encima del planeta"**: se recalcula cada frame a
  partir de la posición YA computada del planeta seleccionado ese
  mismo frame (no un ángulo aparte) — horizontalmente centrado, con el
  borde inferior del cursor a `CURSOR_GAP_PX=4` px del borde superior
  real de ESE planeta en concreto (`planetHalfSize[i]` varía por
  tamaño: 4/8/12px), así que la flecha se pega igual de cerca al
  planeta pequeño que al grande, no queda "flotando" más lejos en unos
  que en otros.
- **`main.c`**: `drawMenu()` ya no dibuja "TAMANO DE MAPA" con flechas
  de texto "< 6 x 4 >" (el cursor visual las sustituye) — solo el
  título, el sistema solar (`Menu_draw()`), el tamaño en texto plano
  como confirmación legible, y "PULSA A PARA EMPEZAR". `Menu_update()`
  se llama cada frame mientras `gameState==STATE_MENU` (las órbitas
  siguen moviéndose aunque no se pulse nada, igual que las patrullas
  de los enemigos durante la partida). `Menu_setVisible(TRUE)` al
  entrar al menú (boot y `resetToMenu()`, spec §28) y `FALSE` justo
  antes de `newGame()`. Botón `START` queda libre/sin uso en el menú;
  `BUTTON_A` confirma — comprobado que no colisiona con el combo de
  reset (`A+B+C+ARRIBA`, spec §28), ya que ese combo se comprueba
  primero y tiene prioridad ese frame, así que pulsar solo A en el
  menú nunca se confunde con el combo completo.
- **Verificado**: build limpio (`make`, sin warnings más allá de los
  ya conocidos de `rom_header.c`, ajenos a este cambio), arranque
  limpio en BlastEm sin errores en el log. La fórmula de
  `F16_computePositionEx` se confirmó leyendo el código fuente de SGDK
  (`src/maths.c`) en vez de asumirla por la documentación, para evitar
  una malinterpretación de qué representa cada parámetro. **No se
  pudo verificar visualmente** en esta sesión (la captura de pantalla
  del emulador no funcionó en este entorno, solo devolvía el
  escritorio) — pendiente de que el usuario lo confirme visualmente
  él mismo.

## 32. Más planetas (tamaños de mapa reales) y órbitas con líneas continuas

Dos peticiones del usuario tras probar el §31: más planetas, y que las
órbitas se vean como líneas continuas en vez de punteadas.

Decisión confirmada (preguntada antes de implementar, ya que afectaba
a la jugabilidad): los planetas nuevos son tamaños de mapa **reales**
y seleccionables, no decoración — de 3 a 5 planetas/presets en total.

**Más planetas**:
- `main.c`: `sizePresets[]` pasa de 3 a 5 entradas, extendiendo la
  misma progresión `+2/+2` que ya tenían las 3 originales:
  `{6,4},{8,6},{10,8},{12,10},{14,12}`. Se extendió hacia arriba
  (mapas más grandes), no hacia abajo — reducir por debajo de 6x4
  arriesgaba agravar el problema de asignación duplicada de items ya
  documentado en el §16 para mapas pequeños. `SIZE_PRESET_COUNT` pasa
  a 5.
- `guidemap.h`: `MAX_MAP_COLS`/`MAX_MAP_ROWS` suben de 10x8 a 14x12
  para dar cabida a los 2 presets nuevos — el coste extra en RAM
  estática de los arrays de scratch de `guidemap.c` (aprox. el doble,
  pero sigue siendo del orden de unos pocos KB) es insignificante
  frente a los 64KB de RAM de la Genesis.
- `menu.h`: `MENU_PLANET_COUNT` pasa a 5 (con su comprobación cruzada
  ya existente contra `SIZE_PRESET_COUNT` del §31).
- **Arte nuevo**: `planet_huge.png` (32x32, 4x4 tiles — el tamaño
  máximo que admite un sprite de hardware en Genesis, 4 tiles por
  eje). Solo existen 4 tamaños de sprite distintos posibles en
  múltiplos de 8px hasta ese límite (8/16/24/32), así que para 5
  planetas los dos más grandes comparten el mismo sprite
  `planetHuge` — no se pierde precisión real, ya que el tamaño exacto
  en celdas siempre se muestra además como texto plano debajo del
  sistema solar.
- `menu.c`: `orbitRadiusX/Y`, `orbitSpeed`, `planetHalfSize` pasan de
  3 a 5 entradas cada uno, con radios/velocidades reescalados para
  que las 5 órbitas quepan holgadas en la pantalla sin invadir el
  texto de arriba/abajo. `Menu_setVisible`'s reinicio de ángulos
  iniciales se generalizó de 3 valores fijos a un bucle
  (`360/MENU_PLANET_COUNT * i`), repartiendo cualquier número de
  planetas uniformemente sin tocar el código si este número vuelve a
  cambiar en el futuro.

**Órbitas como líneas continuas** (antes: puntos aislados por tile,
spec §31):
- `menu_tiles.png` se simplifica a 2 celdas: una de fondo oscuro sin
  uso directo (ancla para que rescomp asigne el índice 0 de paleta al
  fondo, igual que en todos los demás tilesets del juego) y una
  única celda sólida violeta, reutilizada tanto para el sol como para
  las líneas de órbita — ya no hace falta un tile de "punto"
  independiente.
- Nueva `drawTileLine(x0,y0,x1,y1)` en `menu.c`: algoritmo de
  Bresenham clásico, en unidades de TILE (no píxel) — conecta con un
  tramo ininterrumpido de tiles sólidos cada dos muestras consecutivas
  de la elipse, en vez de colocar cada muestra de forma aislada. Es
  la misma categoría de problema que el bug del §30bis
  (`bridgeToSeed`): dos puntos "sueltos" necesitan que se rellenen
  también las celdas intermedias, no solo sus propios extremos, o la
  línea se ve rota allí donde dos muestras consecutivas no caen ya en
  tiles contiguos. `drawOrbit` ahora conecta cada muestra con la
  anterior (y la última con la primera, para cerrar el óvalo) en vez
  de solo colocar un tile por muestra; el paso entre muestras
  (`ORBIT_SAMPLE_STEP_DEG`) pudo subir de 4 a 12 grados (menos
  muestras) precisamente porque ya no depende de caer en tiles
  contiguos por casualidad — `drawTileLine` lo garantiza
  explícitamente.
- **Verificado**: `make clean && make` sin ningún warning (más allá
  de los ya conocidos de `rom_header.c`). Nuevo `test_bigmaps.c`
  (2000 generaciones entre los 2 presets nuevos, 12x10 y 14x12):
  `insertLinkCol/Row` sigue siendo siempre válido y nunca queda
  bloqueado en ningún punto de la partida (misma comprobación central
  que `test_door_offsets.c`/`test_insertion.c` del §30bis, aplicada
  ahora a los tamaños de mapa más grandes que existen) — 0 fallos.
  Re-ejecutados `test_locks.c`, `test_sections.c`, `test_insertion.c`
  y `test_door_offsets.c` (con los presets originales) sin fallos —
  subir `MAX_MAP_COLS`/`MAX_MAP_ROWS` no cambia nada para los tamaños
  que ya funcionaban.
- Verificado en BlastEm: build limpio, arranque limpio sin errores en
  el log tras el rebuild. **Tampoco se pudo verificar visualmente en
  esta sesión** (mismo problema de captura de pantalla que en el
  §31) — sigue pendiente de confirmación visual del usuario.

## 32bis. Revertido el aumento a 5 planetas (líneas continuas se mantienen)

El usuario probó el §32 y pidió revertir solo la parte de "más
planetas", volviendo a los 3 tamaños de mapa originales — las líneas
de órbita continuas (también del §32) no se mencionaron para revertir
y se mantienen sin cambios.

Revertido:
- `main.c`: `sizePresets[]` vuelve a `{6,4},{8,6},{10,8}`;
  `SIZE_PRESET_COUNT` vuelve a 3.
- `guidemap.h`: `MAX_MAP_COLS`/`MAX_MAP_ROWS` vuelven a 10x8.
- `menu.h`: `MENU_PLANET_COUNT` vuelve a 3.
- `menu.c`: `orbitRadiusX/Y`, `orbitSpeed`, `planetHalfSize` vuelven a
  sus 3 valores originales del §31 (`SUN_CENTER_Y` también vuelve de
  136 a 128, ajustado en el §32 solo para dar cabida a las 5 órbitas).
  Se quitan las 2 líneas de `Menu_loadGraphics()` que creaban sprites
  con `planetHuge`.
- Se elimina la entrada `SPRITE planetHuge` de `resources.res` y se
  borra `res/sprite/planet_huge.png` (ya sin ningún uso).

Sin cambios (deliberadamente, no pedido): `drawTileLine()` (Bresenham)
y el resto de la mecánica de líneas continuas del §32 -- `Menu_draw`
sigue conectando cada muestra de la elipse con la anterior en vez de
colocar tiles aislados, con el mismo tile `MENU_TILE_FILL` único.
- **Verificado**: `make clean && make` sin warnings nuevos.
  Re-ejecutados `test_locks.c`, `test_sections.c`, `test_insertion.c`
  y `test_door_offsets.c` sin fallos (vuelven a operar sobre los
  tamaños de rejilla originales, ya cubiertos de sobra por corridas
  previas de estos mismos tests).
- Verificado en BlastEm: build limpio, arranque limpio sin errores en
  el log tras el rebuild. Sigue sin poder verificarse visualmente en
  esta sesión (mismo problema de captura de pantalla).

## 32ter. Órbitas con líneas finas de 2px (auto-tiling por dirección)

Petición del usuario: en vez del tile sólido de 8px (§32) o los
puntos aislados (§31), que las órbitas se vean como líneas finas de
2px de grosor.

Solución (una forma ligera de "auto-tiling"): dado que cada paso
individual del algoritmo de Bresenham solo puede ser uno de 4 tipos
exactos — horizontal, vertical, o una de las dos diagonales — se
crearon 4 tiles nuevos, uno por tipo, y se elige el correcto en cada
paso según su dirección real, en vez de usar siempre el mismo tile
sólido.

Implementación:
- **Arte nuevo** en `menu_tiles.png` (ahora 6 celdas: ancla oscura +
  relleno del sol + 4 líneas finas): `menu_line_h.png` (barra
  horizontal, filas 3-4 de 2px), `menu_line_v.png` (barra vertical,
  columnas 3-4), y dos diagonales de 2px de grosor dibujadas con
  `-strokewidth 2` (`menu_line_bslash.png` de esquina superior-
  izquierda a inferior-derecha, `menu_line_fslash.png` la contraria).
  El relleno sólido del sol se mantiene sin cambios.
- `menu.c`: nueva `orbitTileForStep(dx,dy)` — clasifica el paso según
  `dx==0` (vertical), `dy==0` (horizontal), `dx==dy` (diagonal "\") o
  ninguno de los anteriores (diagonal "/"); son los 4 únicos casos
  posibles que un paso de Bresenham puede producir (cada paso mueve
  como mucho ±1 en cada eje). `drawTileLine` se renombra/reescribe
  como `lineTo(curX,curY,targetX,targetY)`: en vez de recibir dos
  puntos fijos y plantar el tile de inicio Y fin, avanza desde la
  posición ACTUAL (puntero, se actualiza al terminar) hacia el
  objetivo, plantando solo las celdas NUEVAS (la de partida ya la
  plantó la llamada anterior, evitando plantarla dos veces con
  posiblemente dos direcciones distintas) — cada una con el tile que
  corresponde a la dirección real de ESE paso concreto. `drawOrbit`
  planta el primer punto de la elipse a mano (con un tile arbitrario,
  irrelevante entre las ~30 celdas del resto del anillo) y encadena
  llamadas a `lineTo` para cada muestra siguiente, cerrando el óvalo
  al final con una llamada más de vuelta al primer punto.
- **Verificado**: `make clean && make` sin warnings nuevos en
  `menu.c`. Se escribió una simulación aparte en el host
  (matemática en coma flotante estándar, no la trigonometría fix16
  real de SGDK, así que no valida bit a bit el resultado en consola
  pero sí la lógica de conectividad/selección de tile) que renderiza
  los 3 anillos como arte ASCII: confirma que las 3 elipses quedan
  completamente conectadas, sin huecos, y que el carácter elegido en
  cada celda (`-`/`|`/`\`/`/`) sigue la pendiente local real de la
  curva en cada tramo.
- Verificado en BlastEm: build limpio, arranque limpio sin errores en
  el log tras el rebuild. Sigue sin poder verificarse visualmente
  (renderizado real) en esta sesión (mismo problema de captura de
  pantalla que en el §31/§32) — pendiente de confirmación del
  usuario.

## 32quat. Órbitas más amplias

Petición del usuario: las órbitas del menú, más amplias.

Implementación:
- `menu.c`: `orbitRadiusX`/`orbitRadiusY` pasan de `{40,70,100}`/
  `{20,34,48}` (spec §32bis) a `{55,90,130}`/`{26,41,56}` — el eje X
  se ensancha con más margen (la pantalla es de 320px de ancho, sobra
  espacio) que el Y (más limitado por el texto de arriba/abajo).
  `SUN_CENTER_Y` baja de 128 a 120 para repartir mejor el espacio
  vertical disponible entre las órbitas y el texto.
- `main.c`, `drawMenu()`: el título ("OVNI") sube de la fila 6 a la 3,
  y el texto de tamaño/instrucción baja de las filas 24/26 a las
  25/27 (la última fila válida de la pantalla, 28 filas de texto en
  total) — liberando el margen vertical extra que necesitan las
  órbitas más anchas sin que el texto se solape con ellas.
- **Verificado**: `make clean && make` sin warnings nuevos. Se
  reutilizó la simulación en el host del §32ter (matemática en coma
  flotante estándar, no la trigonometría fix16 real de SGDK) con los
  radios nuevos: los 3 anillos siguen quedando completamente
  conectados y dentro de los límites de la rejilla de 40x28 tiles,
  visiblemente más anchos que antes y con margen de sobra respecto al
  sol y a los bordes de la pantalla.
- Verificado en BlastEm: build limpio, arranque limpio sin errores en
  el log tras el rebuild. Sigue sin poder verificarse visualmente
  (renderizado real) en esta sesión — pendiente de confirmación del
  usuario.

## 32quinquies. Órbitas aún más amplias, cerca del límite físico de la pantalla

El usuario pidió una segunda vuelta de "más amplias" tras el §32quat.
Esta vez se calculó explícitamente el techo real de cada eje en vez
de solo escalar a ojo:
- **Eje X**: limitado por el ancho de pantalla (320px, mitad=160)
  menos el semi-ancho del planeta más grande (12px) y un margen
  pequeño (4px) → máximo `160-12-4=144`.
- **Eje Y**: limitado por el hueco entre el título (fila 3) y el
  texto inferior (filas 25/27) menos el espacio que necesita la
  flecha del cursor sobre el planeta más grande (hueco 4px + flecha
  8px + semi-alto del planeta 12px = 24px) — resolviendo las dos
  restricciones (que el borde superior de la órbita más grande no
  choque con el título, que el borde inferior no choque con el texto)
  a la vez da `SUN_CENTER_Y=122` y radio Y máximo `≈62`.
- `orbitRadiusX`/`orbitRadiusY` pasan de `{55,90,130}`/`{26,41,56}`
  (spec §32quat) a `{65,105,144}`/`{30,46,62}` — ya cerca del límite
  físico real, no queda mucho margen para ensanchar más sin tocar
  también la disposición del texto.
- **Verificado**: `make clean && make` sin warnings nuevos.
  Reutilizada la simulación en el host con los radios nuevos: los 3
  anillos siguen conectados y dentro de los límites de la rejilla de
  40x28 tiles (comprobado también a mano: `tx` cae en [2,38] de 40
  columnas, `ty` en [8,23] de 28 filas — ambos con margen, sin
  desbordar).
- Verificado en BlastEm: build limpio, arranque limpio sin errores en
  el log tras el rebuild. Sigue sin poder verificarse visualmente en
  esta sesión — pendiente de confirmación del usuario.

## 32sexies. Sin líneas de órbita, sol blanco

Petición del usuario: "no pintes las lineas de las órbitas y el
centro es una bola blanca".

- Eliminado por completo el dibujado de las órbitas: las funciones
  `orbitTileForStep`, `lineTo` y `drawOrbit` (introducidas en §32ter)
  se borran de `menu.c`, junto con las 4 variantes de tile de línea
  fina (`MENU_TILE_LINE_H/V/BACK/FWD`) y la constante
  `ORBIT_SAMPLE_STEP_DEG`, todas ya sin uso. `Menu_draw()` queda
  reducido a una sola llamada: `drawSun();`.
- `orbitRadiusX[]`/`orbitRadiusY[]` (§32quinquies) se conservan sin
  cambios — siguen marcando la trayectoria elíptica que anima la
  posición de cada planeta en `Menu_update()`, solo que ya no hay
  ninguna línea dibujada bajo esa trayectoria.
- `menu_tiles.png` pasa de un tileset de varias celdas (fondo oscuro +
  4 variantes de línea + relleno violeta) a solo 3 celdas: celda0 =
  fondo oscuro sin uso (ancla para que rescomp asigne el índice0 al
  color oscuro, convención de todo el proyecto), celda1 = placeholder
  violeta sin uso (reserva el índice1, que ya es violeta de forma
  permanente por `Maze_loadGraphics`, para que la celda2 caiga en el
  índice2), celda2 = relleno sólido, con `PAL_setColor(2,
  RGB24_TO_VDPCOLOR(0xFFFFFF))` en `Menu_loadGraphics()` poniendo ese
  índice2 en blanco. `MENU_TILE_FILL` pasa a apuntar a esa celda2.
  Verificado el orden de escaneo raster con
  `magick menu_tiles_v4.png -unique-colors txt:-`: exactamente 3
  colores, en el orden esperado (oscuro, violeta, blanco).
- Nota de recuperación: durante esta sesión se detectó que
  `res/sprite/` había quedado vacío en disco (los 5 PNG trackeados
  por git como borrados sin commit, y los PNG nuevos del menú
  --nunca añadidos a git-- directamente ausentes). Todos los
  orígenes seguían disponibles sin tocar en el scratchpad de trabajo
  (`/tmp/mazepeek/`), así que se restauraron los trackeados con
  `git checkout -- res/sprite/...` y se recopiaron los del menú
  (`menu_tiles_v4.png`, `planet_small/medium/large.png`,
  `cursor_arrow.png`) desde ahí. Ningún asset se perdió, pero conviene
  hacer `git add`/commit de los sprites del menú en algún momento para
  que dejen de depender de esa copia temporal.
- **Verificado**: `make clean && make` sin errores ni warnings nuevos
  (solo los habituales, inofensivos, de `rom_header.c`). `grep` en
  `src/`, `inc/`, `res/` confirma que no queda ninguna referencia a
  los símbolos eliminados (`MENU_TILE_LINE_*`, `orbitTileForStep`,
  `lineTo`, `drawOrbit`, `ORBIT_SAMPLE_STEP_DEG`).
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  sin errores en el log. Sigue sin poder verificarse visualmente en
  esta sesión (limitación de captura de pantalla ya documentada en
  §31/§32) — pendiente de confirmación del usuario.

## 32septies. El sol como sprite circular real, no relleno de tiles

Pregunta del usuario: "pero por que el bloque del centro no es un
círculo?" — tras §32sexies el sol se pintaba rellenando tiles de BG_A
de 8x8 cuyo centro cayera dentro del radio. Con `SUN_RADIUS_PX=12` esa
prueba por tile, calculada a mano, sólo dejaba pasar un bloque limpio
de 2x3 tiles (16x24px) — un rectángulo, cero curvatura, ninguna tile
de esquina pasaba parcialmente. La resolución de 8px por tile es
demasiado gruesa para aproximar un círculo tan pequeño.

- El sol pasa a ser un sprite real de 32x32px (4x4 tiles),
  `res/sprite/menu_sun.png`, con una máscara circular por píxel — la
  misma técnica que ya usan con éxito los sprites de los planetas
  (`planet_large.png` a 24x24 ya se ve razonablemente redondo con este
  método). Generado con ImageMagick dibujando el círculo y luego
  remapeando (`-remap`) a los 2 colores exactos del proyecto para
  eliminar el antialiasing (que habría introducido colores
  intermedios no válidos en una paleta de 4 bits) — confirmado con
  `-unique-colors`: exactamente 2 colores, 471 oscuro / 553 violeta,
  esquina superior-izquierda oscura (así que rescomp escanea oscuro
  primero → index0, violeta segundo → index1).
- Ese orden de escaneo (oscuro=index0, violeta=index1) es EL MISMO que
  usan playerShip, enemyShip, mapShip, los 3 planetas y el cursor — es
  decir, `menu_sun.png` no necesita ningún truco de reserva de índice
  de paleta: compila con la paleta de dos colores ya usada por todos
  esos sprites. Confirmado directamente en `out/release/res/resources.s`:
  el struct compilado de `menuSun` apunta al símbolo
  `playerShip_palette` (rescomp deduplica paletas byte-idénticas), la
  misma paleta que ya usan `cursorArrow`/`planetSmall`/etc. — prueba
  concluyente sin necesitar captura de pantalla.
- Como el color real (violeta) no sirve — el usuario pidió blanco — el
  sprite se engancha a PAL1 (la paleta de playerShip/mapShip) en vez de
  crear una paleta nueva o pelear por un índice libre en PAL0/PAL3
  (los 4 líneas de paleta del Genesis ya estaban repartidas: PAL0
  mapa/texto, PAL1 nave/mapShip, PAL2 enemigos, PAL3 planetas/cursor).
  playerShip y mapShip están SIEMPRE ocultos mientras se muestra el
  menú (main.c los oculta en el arranque y en `resetToMenu()`), así
  que nada más depende del color de PAL1 índice1 en ese momento.
  `Menu_setVisible()` ahora hace `PAL_setColor(SUN_INK_INDEX, ...)`
  para poner ese slot en blanco al mostrar el menú, y lo restaura al
  violeta original de `playerShip.palette->data[1]` al ocultarlo —
  exactamente el mismo patrón de "override temporal + restauración"
  que ya usaba el código del parpadeo del §23
  (`PLAYER_SHIP_INK_INDEX`), sólo que aplicado desde `menu.c` en vez
  de `main.c`.
- Eliminado por completo lo que quedaba de la versión basada en tiles
  de BG: `menuTiles` (TILESET + `menu_tiles.png`), `MENU_TILE_BASE`/
  `MENU_TILE_FILL`, `putMenuTile()`, `drawSun()`, y `Menu_draw()`
  entera (ya no queda nada que dibujar en BG_A para el sol/planetas,
  que ahora son sprites autónomos) — junto con sus 3 referencias en
  `main.c` (dentro de `drawMenu()`) y su declaración en `menu.h`.
- **Verificado**: `make clean && make` sin errores ni warnings nuevos.
  `grep` confirma cero referencias sueltas a los símbolos eliminados.
  Inspección directa de `out/release/res/resources.s`: `menuSun`
  comparte paleta con `playerShip` (confirma el mapeo de índices sin
  necesitar ejecutar el juego).
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  sin errores en el log. Sigue sin poder verificarse visualmente en
  esta sesión — pendiente de confirmación del usuario de que ahora sí
  se ve como un círculo blanco.

## 33. Fases más pequeñas (1, 2, 3 y 4 letras), añadidas al menú de planetas

Petición del usuario: "Haz fases mas pequeñas. Con 1,2,3,4 letras y con
grids de habitaciones mas pequeñas. Incluyelas en el menu de los
planetas".

- Hasta ahora `ITEM_COUNT` (número de letras) era una constante de
  compilación fija en 5, usada en todas partes (`guidemap.c`,
  `items.c`) — cada partida colocaba siempre exactamente 5 letras,
  fuera cual fuera el preset de tamaño. Pasa a ser una variable en
  tiempo de ejecución, `itemCount`, con el mismo patrón ya establecido
  para `mapCols`/`mapRows` vs `MAX_MAP_COLS`/`MAX_MAP_ROWS`: `ITEM_COUNT`
  sigue existiendo como el máximo (dimensiona los arrays estáticos
  `itemCol[]`/`itemRow[]`/`collected[]`/el buffer del HUD), pero cada
  bucle que antes iteraba hasta `ITEM_COUNT` ahora itera hasta el
  `itemCount` activo de esa partida (`guidemap.c`: `itemIndexAtRoom`,
  `selectItemRooms`, el bucle de "letra siempre visible" de
  `GuideMap_drawOverlay`; `items.c`: reset, `findItemAt`,
  `Items_tryCollect`, `Items_drawHud`).
- `main.c`: `SizePreset` gana un tercer campo, `letters`. 4 presets
  nuevos, más pequeños que el mínimo anterior (6x4), añadidos ANTES de
  los 3 originales (que se mantienen sin cambios, siempre con 5
  letras):
  ```
  { 3, 3, 1 },
  { 4, 3, 2 },
  { 5, 3, 3 },
  { 5, 4, 4 },
  { 6, 4, 5 },   // original más pequeño
  { 8, 6, 5 },   // original medio
  { 10, 8, 5 },  // original más grande
  ```
  `newGame()` ahora también fija `itemCount = sizePresets[...].letters`
  junto a `mapCols`/`mapRows`, antes de `GuideMap_generate()`. El texto
  del menú (`drawMenu()`) muestra ahora también el número de letras
  ("6 x 4 - 5 LETRAS"), ya que tamaño de grid y número de letras varían
  de forma independiente y ambos importan para saber la dificultad
  real del preset elegido.
- **Bug real encontrado por fuzzing (no reportado por el usuario,
  encontrado proactivamente al validar los presets nuevos)**:
  `selectItemRooms()` (farthest-point sampling de las salas donde van
  las letras) podía elegir la MISMA sala sin querer dos veces. La
  puntuación de una sala ya elegida sólo puede bajar con el tiempo
  (su distancia a sí misma es 0), así que en cuanto TODAS las salas
  candidatas restantes degradan también a puntuación 0 — algo mucho
  más fácil de alcanzar en un grid pequeño con pocos callejones sin
  salida en relación al número de letras pedido — la comparación `>=`
  del algoritmo podía volver a seleccionar esa misma sala ya elegida
  en una ronda posterior, produciendo una entrada duplicada en
  `itemCol[]/itemRow[]`. Eso deja una de las letras sin sala propia:
  `Items_tryCollect` nunca la reconoce como "la siguiente debida" en
  esa posición (porque `findItemAt` encuentra primero la ocurrencia
  anterior, ya recogida), atascando la partida sin poder terminar.
  Reproducido incluso en el preset ORIGINAL de 6x4/5 letras (no es un
  bug introducido por los presets nuevos, sólo mucho más fácil de
  disparar con ellos: ~0.03% de las semillas en 6x4/5 letras, frente a
  0% observado en los presets nuevos con este primer fix). Corregido
  eliminando la sala ya elegida del array de candidatas (swap-remove,
  mismo patrón que ya usa el consumo del `frontier` en `carveTree`) en
  vez de sólo poner su puntuación a 0.
- Tras ese primer fix seguía habiendo un segundo caso, más raro
  (2 fallos en 21000 simulaciones): cuando el grid es TAN pequeño que
  se queda sin candidatas genuinas para más de una letra, el único
  fallback (la sala de inicio) se reutilizaba para varias letras a la
  vez — el mismo bug, sólo que en la sala de inicio en vez de en un
  callejón sin salida. Corregido: la sala de inicio sólo puede servir
  de fallback para UNA letra; si el grid es tan pequeño que ni siquiera
  eso basta, `itemCount` se reduce para el resto de esa partida
  concreta en vez de crear un duplicado incobrable — mejor ofrecer
  menos letras de las nominales que una letra imposible de recoger.
- `menu.c`/`menu.h`: `MENU_PLANET_COUNT` pasa de 3 a 7. Sólo existen 4
  tamaños de sprite discretos en el hardware real (1 a 4 tiles por
  lado, y el de 4 tiles ya lo usa el sol, spec §32septies), así que con
  7 planetas cada tamaño de sprite se reutiliza en un grupo de niveles
  contiguos (pequeño ×3, medio ×2, grande ×2) — el radio de la órbita
  (más cerca = preset más pequeño/fácil) pasa a ser la señal visual
  principal de progresión entre los 7, no el tamaño del sprite.
  `orbitRadiusX[]`/`orbitRadiusY[]` recalculados desde cero para 7
  anillos: el anillo exterior (el preset 10x8 original) mantiene
  exactamente el mismo techo físico que antes (X=144/Y=62); el anillo
  interior (el preset nuevo 3x3/1 letra) se sitúa lo bastante lejos del
  propio sprite del sol (radio visual ~13px) más el medio-ancho del
  planeta (4px) más un margen (X=46/Y=20); los 5 anillos intermedios
  se interpolan linealmente entre esos dos extremos en cada eje por
  separado, manteniendo la misma proporción X/Y (~2.32) que ya tenía
  el anillo exterior.
- **Verificado**: nuevo test de fuzzing en el host
  (`test_small_phases.c`, scratchpad de la sesión) que simula una
  partida COMPLETA (generar mapa, recalcular locks, recoger cada letra
  en orden en su posición real, recalculando locks tras cada una) para
  los 7 presets × 20000 semillas cada uno = 140000 simulaciones — 0
  fallos tras los dos fixes anteriores (71 fallos con el primer bug
  sin corregir, 2 fallos tras el primer fix pero antes del segundo).
  `make clean && make` sin errores ni warnings nuevos. `grep` confirma
  que las únicas referencias a `ITEM_COUNT` que quedan son,
  correctamente, las de dimensionado de arrays (el máximo), nunca
  bucles activos.
- Verificado en BlastEm: cerrada la instancia anterior de este mismo
  proyecto que aún corría desde una prueba previa de esta sesión, se
  lanzó una nueva con la ROM reconstruida; proceso estable varios
  segundos, log sin errores. Sigue sin poder verificarse visualmente
  en esta sesión — pendiente de que el usuario confirme que los 7
  planetas y sus tamaños/letras se ven y se seleccionan correctamente.

## 34. Victoria al volver a la nave con todas las letras

Petición del usuario: "cuando recoges todas las letras puedes volver a
la nave. Se vuelve por la habitación de extracción. En realidad la
nave puede volver cuando quiera. Cada planeta tiene el recuento de sus
letras." Aclarado con 3 preguntas: (1) al volver a la nave con todas
las letras, pantalla de victoria y vuelta al menú; (2) se puede volver
en cualquier momento como ya pasaba, pero distinguiendo visualmente
una salida incompleta de una completa; (3) el recuento de letras por
planeta ya estaba resuelto en el §33 (texto bajo el menú al
seleccionar un planeta) — nada que cambiar ahí.

- Volver por la habitación de inserción (spec §27/§29) YA era posible
  en cualquier momento, con o sin todas las letras — eso no cambia.
  Lo que cambia es el DESTINO de ese viaje de vuelta:
  - Con `Items_allCollected()` (nueva función en `items.h`/`.c`,
    `nextIndex >= itemCount`) en TRUE: fase completada. Nuevo estado
    `STATE_WIN` en el `GameState` de `main.c` — pantalla fija
    ("FASE COMPLETADA" / "PULSA A PARA VOLVER AL MENU") sobre BG_A,
    nave y enemigos ocultos, esperando BUTTON_A para volver al menú
    (reutiliza `resetToMenu()` tal cual).
  - Con `Items_allCollected()` en FALSE: exactamente el comportamiento
    de siempre — vuelve a una habitación de inserción recién generada,
    puede volver a entrar en el mapa cuando quiera.
- Distinción visual pedida en la sala de extracción (spec §34,
  aclaración 2): mientras la nave está en la habitación de inserción
  y aún faltan letras, un mensaje fijo ("AUN FALTAN LETRAS") en BG_B
  fila 2, con la misma técnica de alta prioridad que ya usan el HUD de
  letras (fila 1) y el contador de FPS (fila 0) para verse por encima
  del laberinto de BG_A. Se muestra al entrar (spawn inicial en
  `newGame()` y cada vuelta desde el mapa) y se borra al volver a
  salir hacia el mapa. Nunca se muestra en el caso completo — ese caso
  ya no vuelve a la habitación de inserción en absoluto, va directo a
  la pantalla de victoria, así que la propia aparición de esa pantalla
  ES la distinción.
- Efecto colateral corregido de paso: `resetToMenu()` no limpiaba
  nunca las filas 1/2 de BG_B (HUD de letras / este nuevo aviso) —
  como esas filas sólo se redibujan durante la partida y
  `VDP_clearPlane` del menú sólo toca BG_A (un plano distinto), un
  reset a mitad de partida (combo de reinicio, o ahora también
  `STATE_WIN`) podía dejarlas superpuestas sobre el menú. Se añaden 2
  `VDP_clearTextLineBG(BG_B, ...)` en `resetToMenu()` para las dos
  filas. No reportado por el usuario, encontrado al revisar el reuso
  de `resetToMenu()` desde el nuevo flujo de victoria.
- **Verificado**: `make clean && make` sin errores ni warnings nuevos.
  Revisión manual del flujo de control (no fuzzeable por el host al
  depender de `JOY_readJoypad`/sprites reales de SGDK): confirmado que
  tras entrar en `STATE_WIN` el resto del frame (reposicionar
  `playerSprite`, el bloque de recogida/patrulla) queda correctamente
  saltado porque `inInsertRoom` se pone a TRUE en ambas ramas (como ya
  hacía la rama gemela antes de este cambio), y que `Items_h`/
  `resources.h` ya estaban incluidos donde se necesitaba
  (`Items_allCollected` visible desde `main.c` vía `items.h`).
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme en pantalla el aviso
  de "AUN FALTAN LETRAS" y la pantalla de victoria al completar una
  fase.

## 35. Progreso guardado por planeta, visible en el menú

Petición del usuario: "quiero que cada planeta tenga el recuento de
sus letras obtenidas. Si el player sale de un planeta, esas letras se
conservan. En la UI del menu si indican las letras recogidas y las
que faltan."

- Nuevo `PresetSave { bool hasSave; u16 mapSeed; u8 collectedCount; }`
  en `main.c`, un array `presetSave[SIZE_PRESET_COUNT]` (uno por
  planeta, arranca a cero = sin guardar). Como recoger letras siempre
  es estrictamente en orden (spec §13, `Items_tryCollect` lo obliga),
  basta con guardar CUÁNTAS llevas — nunca hace falta un bitmask,
  "N recogidas" siempre significa exactamente los índices 0..N-1.
- **Hallazgo importante al implementar esto**: `mapSeed` ya existía
  (`main.c`, spec §5) pero SÓLO se usaba para el hash determinista de
  cada habitación individual (`roomSeedFor`) — nunca se usaba para
  hacer determinista la propia `GuideMap_generate()` (topología del
  árbol de salas, posición de las letras, enlace de inserción, meta).
  `GuideMap_generate()` tira de `random()`, que lee el stream global
  del PRNG de SGDK — un stream que sigue avanzando con cada llamada a
  `random()` en cualquier parte del juego (otras salas, enemigos...),
  así que releer el mismo `mapSeed` más tarde NO reproducía el mismo
  mapa. `maze.c` ya resolvía este mismo problema a nivel de habitación
  con `setRandomSeed(roomSeed)` al principio de `Maze_generateRoom`/
  `Maze_generateInsertionRoom` (líneas 251/332) — `newGame()` ahora
  aplica la misma idea a nivel de mapa completo: `setRandomSeed(mapSeed)`
  justo antes de `GuideMap_generate()`, tanto en una partida nueva
  (mapSeed recién sorteado) como al reanudar (mapSeed guardado) — así
  el mismo mapSeed reproduce EXACTAMENTE el mismo mapa, sin importar
  cuánto haya avanzado el stream global mientras tanto.
- `newGame()`: si `presetSave[sizePresetIndex].hasSave`, reutiliza su
  `mapSeed` guardado (en vez de sortear uno nuevo) y, tras
  `Items_reset()`, llama a la nueva `Items_fastForward(collectedCount)`
  (`items.c`) para marcar como recogidas las primeras `collectedCount`
  letras de golpe, sin comprobaciones de posición/colisión (no aplica,
  es una restauración de estado, no una recogida física real).
  `GuideMap_recomputeLocks()` se sigue llamando DESPUÉS, para que los
  candados reflejen correctamente qué letra toca ahora.
- `resetToMenu()`: antes de cambiar `gameState`, mira el estado
  ANTERIOR (`STATE_PLAYING` o `STATE_WIN`, nunca si ya estaba en el
  menú — evita sobrescribir con datos obsoletos si se pulsa el combo
  de reset estando ya en el menú) y guarda el progreso del planeta que
  se acaba de dejar: si `Items_allCollected()`, borra su guardado
  (`hasSave=FALSE` — completar un planeta es "empezar de cero" la
  próxima vez, sólo ABANDONAR a medias es lo que debe poder
  reanudarse); si no, guarda `hasSave=TRUE`, el `mapSeed` actual y
  `Items_collectedCount()`.
- `drawMenu()`: nueva línea (fila 26, entre el tamaño/letras en la 25
  y "PULSA A" en la 27) con "N DE M RECOGIDAS" para el planeta
  seleccionado — usa `collectedCount` guardado si `hasSave`, o 0 si
  nunca se ha jugado ese planeta (o si se completó y su guardado se
  borró), mostrando siempre el estado real, no el de la partida en
  curso.
- Efecto colateral menor corregido de paso: `resetToMenu()` ya
  limpiaba las filas 1/2 de BG_B (spec §34); sin cambios adicionales
  ahí, sólo se confirma que sigue siendo coherente con el nuevo guardado
  (limpiar el HUD visual no afecta al guardado en memoria, son cosas
  separadas).
- **Verificado**: nuevo test de fuzzing en el host
  (`test_resume.c`, scratchpad de la sesión) que simula exactamente el
  escenario real: genera un mapa con un `mapSeed`, avanza el stream
  global con 500 llamadas a `random()` de "ruido" MÁS una generación
  completa de un preset totalmente distinto (simulando otra partida
  intercalada), y comprueba que reaplicar `setRandomSeed(mapSeed)` +
  `GuideMap_generate()` reproduce exactamente la misma rejilla,
  posiciones de letras, enlace de inserción y meta — 7 presets × 5000
  semillas = 35000 simulaciones, 0 fallos. También comprueba el
  camino de `Items_fastForward`: tras reanudar con progreso parcial,
  el enlace de inserción sigue desbloqueado y la siguiente letra
  pendiente sigue siendo recogible en su habitación real. (Dos fallos
  de comparación en iteraciones tempranas del test resultaron ser del
  propio test, no del juego: comparaba la rejilla completa
  `MAX_MAP_ROWS×MAX_MAP_COLS` en vez de sólo la región activa
  `mapRows×mapCols`, y comparaba `doorOffsetX`/`itemCol[]` más allá de
  cuándo son significativos — corregido en el test antes de confiar en
  el resultado.)
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme que la línea "N DE M
  RECOGIDAS" se ve bien en el menú y que abandonar un planeta a medias
  y volver a él lo reanuda con el mismo mapa y las mismas letras ya
  recogidas.

## 36. Segunda puerta en la sala de extracción, para volver al menú

Petición del usuario: "tiene que haber una salida en la sala de
extracción para que el usuario vuelva a salir al menu. Por ahi entra."

Hasta ahora la sala de inserción/extracción tenía una única puerta
(la "puerta de misión", hacia/desde `insertLinkCol/Row`): volver al
menú sólo era posible completando todas las letras (spec §34) o con
el combo de reset (A+B+C+UP). El usuario pide una puerta física
adicional, en esa misma sala, dedicada a volver al menú a voluntad.

- `Maze_generateInsertionRoom` (maze.c/maze.h) gana dos parámetros:
  `menuDoorDir`, `menuDoorOffset`. Siempre perpendicular a la puerta
  de misión (`main.c` la deriva como `(insertRoomDoorDir + 1) & 3`),
  así que nunca puede coincidir ni ser opuesta a ella. Refactorizado
  el volcado de la puerta (antes un único `switch` inline) a una
  función compartida `punchBorderDoor(dir, anchorX, anchorY)`, usada
  para las dos puertas — evita que la lógica de las dos puertas pueda
  divergir con el tiempo. `carve()` ahora recibe 2 `forcedTarget`
  (antes 1), y `bridgeToSeed` se llama para cada ancla que no haya
  quedado ya conectada por el propio carve.
- El offset de la puerta de misión sigue viniendo de `insertLinkOffset`
  (debe coincidir con la puerta real del lado opuesto, spec §30). El
  de la puerta de menú es fijo/centrado (`MAZE_W/2` o `MAZE_H/2` según
  eje) — nunca necesita alinearse con ninguna otra sala, así que no
  hace falta aleatorizarlo; se calcula una sola vez en `main.c`
  (`newGame()`) y se pasa como parámetro, evitando duplicar la fórmula
  de centrado en dos archivos.
- `main.c`: nuevas `menuDoorDir`/`menuDoorOffset` (estáticas, fijadas
  una vez por partida igual que `insertRoomDoorDir`). El manejo por
  frame de la sala de inserción ahora marca AMBAS puertas como activas
  para `Player_updateRoom` (antes sólo una), con el offset
  correspondiente a cada una. Cruzar la puerta de misión se comporta
  igual que siempre (spec §29/§34: a la rejilla, o victoria si ya
  estaban todas las letras); cruzar la puerta de menú llama
  directamente a `resetToMenu()` — la misma función que ya usa el
  combo de reset, así que el guardado de progreso (spec §35) se aplica
  automáticamente sin lógica nueva.
- **Verificado**: nuevo test de fuzzing en el host
  (`test_insert_two_doors.c`, scratchpad de la sesión), reutilizando
  la técnica BFS de accesibilidad ya usada para el bug de §30bis:
  genera la sala de inserción con las 12 combinaciones válidas de
  dirección (puerta de misión × puerta de menú, excluyendo cuando
  coinciden) y un rango de offsets variado, para 20000 semillas cada
  una — 240000 generaciones en total, confirmando que AMBAS puertas
  quedan siempre conectadas a la semilla de la sala (nunca una sala
  con una puerta físicamente inalcanzable). 0 fallos.
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme que la segunda puerta
  aparece en la sala de extracción y que cruzarla vuelve al menú.

## 37. La sala de extracción en el mapa, y una flecha señalando la salida

Petición del usuario: "quiero que la habitación de inserción/
extracción figure en el mapa. Y quiero que indiques con una flecha la
salida dentro de esta habitación."

Dos partes separadas, una en el mapa-guía (`GuideMap_drawOverlay`,
tecla C) y otra dentro de la propia sala mientras se juega (BG_A,
igual que las letras de los items o el HUD).

- **La sala en el mapa** (`guidemap.c`): siempre se dibuja (nunca
  sujeta a fog-of-war -- el jugador siempre la ha "visitado", es donde
  empieza toda partida), justo fuera de la rejilla, en el lado de
  `insertLinkDir` respecto a `(insertLinkCol,insertLinkRow)` — la
  misma relación espacial que ya tiene la puerta física real
  (maze.c/main.c). Relleno sólido, mismo aspecto que cualquier sala
  visitada. Un tramo de corredor conecta su caja con la sala periférica,
  con la misma convención de hueco de 1 tile que ya usan los corredores
  normales entre salas.
  - **Problema de espacio detectado y resuelto**: el preset más grande
    (10x8) deja casi cero margen alrededor de la propia rejilla dentro
    de la pantalla de 40x28 tiles (`totalW=39` de 40 columnas
    disponibles). Colocar la sala de extracción exactamente adyacente
    no siempre cabe en pantalla para los presets grandes. Se resuelve
    con un CLAMP: la posición calculada se recorta para quedar siempre
    dentro de la pantalla (se dibuja "lo más cerca posible" en vez de
    fuera de plano) — degradación aceptable sólo en el borde extremo
    de los presets más grandes, exacta en el resto. El tramo de
    corredor lleva su propia comprobación de límites (a diferencia del
    resto del código de corredores, que nunca la necesita porque un
    vecino real siempre está dentro de la rejilla).
- **La flecha dentro de la sala** (`main.c`, nueva `drawInsertRoomArrow()`):
  un carácter ASCII (`^`/`v`/`<`/`>` según `insertRoomDoorDir`) dibujado
  con `VDP_drawText` directamente sobre BG_A, sin arte nuevo — mismo
  enfoque que ya usan las letras de los items o el HUD. Se coloca justo
  dentro de la puerta de MISIÓN únicamente (no la puerta de menú del
  §36, que es una acción secundaria/opcional; ésta es la que toda
  partida necesita encontrar), centrada en su ancho de 2 celdas de
  laberinto usando el mismo `insertLinkOffset` con el que la propia
  puerta ya se construye. Llamada tras cada `Maze_draw()` de la sala de
  inserción (spawn inicial y cada regreso).
- **Verificado**: nuevo test en el host (`test_insert_on_map.c`,
  scratchpad de la sesión) con una variante del shim `fakeinc` que
  esta vez SÍ comprueba límites (`fakeinc_bounds/genesis.h`, en vez
  de aceptar cualquier coordenada como el shim original): genera un
  mapa completo, marca todas las salas como visitadas (peor caso para
  el bucle principal) y llama a `GuideMap_drawOverlay()` de verdad,
  para los 7 presets × 5000 semillas = 35000 llamadas — 0 escrituras
  de tile fuera de los límites 40x28 reales. Para la flecha (sólo en
  `main.c`, no se puede compilar en el host por depender del bucle
  principal de SGDK): verificado a mano que `insertLinkOffset` siempre
  cae en [2,16] (N/S) o [2,10] (E/W) -- rango ya establecido y
  fuzzeado en specs anteriores (§30) --, así que `tx`/`ty` siempre caen
  dentro de [0,40)/[0,28) por construcción, sin necesitar clamp.
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme que la sala de
  extracción aparece en el mapa-guía y que la flecha señala
  correctamente la puerta de misión dentro de la sala.

## 38. Bug: completar la fase de 1 letra mostraba "0 de 1" en el menú

Reporte del usuario: "hay un bug, la habitación con 1 letra, si la
completo, en el menu aparece 0 de 1. Debería ser completa."

Causa: `resetToMenu()` (spec §35) BORRABA el guardado
(`hasSave=FALSE`) cada vez que `Items_allCollected()` era TRUE al
volver al menú, con la idea de que "completar es empezar de cero la
próxima vez". Pero eso significa que justo DESPUÉS de ganar, el
guardado de ese planeta deja de existir, y `drawMenu()` -- al no
haber guardado -- muestra `collected=0` en vez de reflejar que
`collectedCount` habría sido igual a `letters` (fase completa). Esto
reproducía el bug exacto que reporta el usuario para CUALQUIER
preset, no sólo el de 1 letra -- simplemente es más notorio ahí
porque "0 de 1" salta más a la vista que "0 de 5".

- Arreglado quitando esa rama especial: `resetToMenu()` ahora siempre
  guarda `hasSave=TRUE`, `mapSeed` y `Items_collectedCount()` al salir
  de una partida real (`STATE_PLAYING` o `STATE_WIN`), sin excepción
  para el caso completo. Cuando la partida se ganó,
  `Items_collectedCount()` ya vale exactamente `itemCount` de forma
  natural (por eso se pudo ganar), así que el guardado queda
  correctamente como "completo" y el menú lo muestra así
  ("N DE N RECOGIDAS").
- Efecto secundario aceptado, no reportado como problema: si se
  vuelve a seleccionar un planeta ya completado, `newGame()` reanuda
  ese mismo mapa (mismo `mapSeed`) con todas las letras ya marcadas
  como recogidas (`Items_fastForward(itemCount)`) -- caminar hasta la
  puerta de misión dispara la victoria de inmediato, ya que no queda
  nada por recoger. No se ha pedido un "empezar de cero tras
  completar", así que no se añade esa lógica extra; el guardado sólo
  necesitaba dejar de borrarse para que el menú mostrara el estado
  real.
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme que completar la fase
  de 1 letra (y cualquier otra) ahora muestra "N DE N RECOGIDAS" en el
  menú en vez de "0 DE N".

## 39. La victoria se decide al salir de la sala, no al entrar en ella

Petición del usuario: "para completar la fase, tiene que salir de la
habitación de inserción/extracción con todas las letras recogidas."

Hasta ahora (spec §34) la victoria se disparaba en el momento de
CRUZAR desde la rejilla HACIA la sala de inserción (con todas las
letras ya recogidas) — la sala de extracción en sí nunca llegaba a
mostrarse en ese caso, se saltaba directamente a la pantalla de
"FASE COMPLETADA". El usuario pide que sea al revés: hay que llegar
primero a la sala (como siempre), y la fase sólo se completa cuando
el jugador la ABANDONA por su propia puerta de salida (la puerta de
menú, spec §36) teniendo ya todas las letras.

- La rama "volvió por el enlace de inserción" (dentro de la sala
  periférica, `isInsertLinkRoom && exitDir==insertLinkDir`) pierde por
  completo el `if (Items_allCollected())`: ahora SIEMPRE regenera la
  sala de inserción sin más, exactamente igual que cuando aún faltan
  letras — nunca dispara la victoria directamente.
- La comprobación se traslada a la rama de la puerta de MENÚ dentro
  de la propia sala de inserción (`else if (exitDir ==
  exitDirForDoorDir(menuDoorDir))`, spec §36): si
  `Items_allCollected()` es TRUE al cruzarla, dispara la pantalla de
  victoria (`STATE_WIN`) en vez de volver directamente al menú; si es
  FALSE, se comporta exactamente igual que antes (`resetToMenu()`,
  guarda el progreso, spec §35).
- Consecuencia directa, coherente con lo que pide el usuario: ahora el
  jugador SIEMPRE ve/atraviesa la sala de extracción -- con la flecha
  señalando la puerta de misión (spec §37) y, si ya tiene todo, puede
  simplemente girar y salir por la puerta de menú para completar la
  fase; nada cambia en la puerta de misión en sí, que sigue llevando
  siempre a la rejilla igual que antes.
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme que, con todas las
  letras recogidas, volver a la sala de extracción YA NO completa la
  fase por sí solo, y que hace falta salir por la puerta de menú para
  que aparezca "FASE COMPLETADA".

## 40. Dos esquemas de control: NORMAL (nuevo, por defecto) y BORRACHO

Petición del usuario: "haz un modo de control normal cada direccion a
su tecla de cursor. Y presionando arriba y B+C se activa el modo
'borracho' qué es el esquema de control que hay ahora."

El control que este juego siempre tuvo (heredado del js13k original)
es: la nave avanza sola cada frame en la dirección a la que está
"mirando", rebotando al chocar contra un muro, y LEFT/RIGHT sólo giran
esa dirección 90° en vez de mover directamente -- de ahí el nombre
"borracho" que le da el usuario, describe bien lo desorientador que
es frente a un control directo. Se convierte en un modo alternativo;
el nuevo modo NORMAL (control directo, cada dirección del D-pad mueve
la nave hacia ahí mientras se mantenga pulsada) pasa a ser el
predeterminado.

- `player.h`/`player.c`: nuevo `DIR_NONE` (ningún valor de dirección
  activo -- sólo posible en modo NORMAL, la nave simplemente no se
  mueve). `movePlayer()` gana un parámetro `bounceOnWall`: TRUE
  (borracho) invierte `p->dir` al chocar, exactamente el
  comportamiento de siempre; FALSE (normal) deja `p->dir` y la
  posición intactos al chocar -- la nave se queda quieta contra el
  muro en vez de rebotar. `Player_updateRoom()` gana un parámetro
  `drunkMode` que se limita a reenviar ese valor a `movePlayer`; la
  detección de salida por puerta no cambia (sigue mirando `p->dir`,
  que ahora simplemente puede valer `DIR_NONE` sin coincidir con
  ningún caso, sin necesitar lógica nueva ahí).
- `main.c`: nuevo `ControlMode` (`CONTROL_NORMAL`/`CONTROL_DRUNK`),
  arranca en NORMAL, persiste entre partidas/menú (es una preferencia
  del jugador, no parte del estado de una partida concreta). Combo de
  activación `DRUNK_TOGGLE_COMBO = BUTTON_B|BUTTON_C|BUTTON_UP` --
  alterna entre los dos modos, comprobado justo después del combo de
  reset ya existente (A+B+C+UP) en el bucle principal: como
  `DRUNK_TOGGLE_COMBO` es un subconjunto de `RESET_COMBO` (le falta
  sólo A), mantener pulsados los 4 botones a la vez siempre resuelve
  como el reset solo, nunca ambos a la vez (el `if`/`else if` ya
  existente lo garantiza sin lógica extra).
- El manejo de input en `STATE_PLAYING` se bifurca por modo: en
  BORRACHO, LEFT/RIGHT rotan (edge-triggered, comportamiento idéntico
  al de siempre); en NORMAL, se lee el D-pad completo cada frame
  (UP/DOWN/LEFT/RIGHT, sin diagonales, prioridad en ese orden) y se
  asigna directamente a `player.dir` (o `DIR_NONE` si no hay ninguna
  dirección pulsada). Los 2 sitios donde se llama a
  `Player_updateRoom()` pasan `controlMode == CONTROL_DRUNK` como
  nuevo argumento.
- Indicador en pantalla (nuevo, para poder verificar sin capturas):
  "NORMAL" o "BORRACHO" en la esquina superior izquierda de BG_B,
  misma técnica de alta prioridad que ya usa el contador de FPS
  (esquina superior derecha) -- visible tanto en el menú como en
  partida, para saber en todo momento qué modo está activo.
- **Verificado**: nuevo test en el host (`test_control_modes.c`,
  scratchpad de la sesión) con un `Maze_isWall` de prueba (muro fijo
  en una columna conocida) para poder predecir el punto exacto de
  colisión: confirma que el modo BORRACHO sigue rebotando exactamente
  igual que antes (regresión), que el modo NORMAL se queda quieto sin
  invertir dirección al chocar (ni se mueve más en frames
  posteriores), y que `DIR_NONE` no mueve la nave en absoluto. (La
  primera versión del test fallaba por una suposición incorrecta del
  propio test sobre dónde se detiene la nave exactamente --
  `collideRight` sondea `x+BOX` con `BOX=MAZE_TILE_PX-2=14`, no la
  caja completa de 16px, así que el punto de parada real no cae en un
  borde de celda limpio; corregido el test antes de confiar en el
  resultado.)
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme que el modo NORMAL
  mueve la nave directamente con el D-pad, que UP+B+C activa/desactiva
  el modo BORRACHO (el control original), y que el indicador de modo
  se ve en la esquina superior izquierda.

## 41. Modo NORMAL: velocidad constante, no hay que mantener pulsado

Petición del usuario (tras confirmar que el mapeo del §40 iba bien):
"bien pero que la velocidad sea constante como el borracho."

`movePlayer()` siempre mueve exactamente 1px/frame en ambos modos --
en eso ya eran idénticos. Lo que no era constante era el modelo de
INPUT: el §40 leía el D-pad como "mientras se mantenga pulsado" (nivel,
no flanco), así que la velocidad caía a 0 en el instante de soltar el
botón -- un patrón de arranca-para, no una velocidad constante como la
del modo borracho (que nunca se detiene salvo al chocar). Reinterpretado
"velocidad constante como el borracho" como: una vez fijada una
dirección, la nave debe seguir moviéndose sola a ese ritmo constante
sin necesidad de mantener nada pulsado -- igual que el borracho nunca
para -- y sólo cambia con una pulsación NUEVA de otra dirección, o se
detiene (sin rebotar, spec §40) al chocar contra un muro.

- `main.c`: la rama `CONTROL_NORMAL` del manejo de input pasa de nivel
  (`state & BUTTON_X`) a flanco (`(state & BUTTON_X) &&
  !(prevState & BUTTON_X)`) para las 4 direcciones, y se quita el
  `else player.dir = DIR_NONE;` que antes forzaba parada al soltar --
  ahora `player.dir` sólo cambia con una pulsación nueva, permanece
  igual el resto del tiempo (incluida cuando no hay nada pulsado), y
  `Player_updateRoom`/`movePlayer` (sin cambios desde el §40) ya se
  encargan de mover 1px/frame constantemente mientras no haya
  colisión.
- Efecto colateral necesario: `Player_spawnAtRoomCenter` fija
  `dir=DIR_DOWN` por defecto (pensado para que el modo BORRACHO
  arranque avanzando solo, como el original). Con el nuevo modelo de
  flanco del modo NORMAL, si no se corrige, la nave arrancaría
  moviéndose sola hacia abajo desde el primer frame sin que el
  jugador tocara nada (el flanco nunca se dispara para forzar
  `DIR_NONE`). `newGame()` ahora sobreescribe `player.dir = DIR_NONE`
  justo después del spawn cuando `controlMode == CONTROL_NORMAL`, así
  que la nave empieza quieta hasta la primera pulsación, coherente con
  un control directo.
- No se ha tocado `player.c` en este cambio -- toda la lógica de
  movimiento/colisión del §40 (incluido su test en el host) sigue
  siendo válida sin cambios; esto es puramente cómo `main.c` traduce
  el joypad a `player.dir` en modo NORMAL. No hay una forma nueva de
  testear esto en el host (depende del bucle principal real de SGDK),
  así que se verificó a mano releyendo la lógica de flancos y el
  punto de reseteo en el spawn.
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme que en modo NORMAL
  basta con pulsar una vez una dirección para que la nave siga
  moviéndose sola a velocidad constante hasta chocar o hasta la
  siguiente pulsación.

## 42. Modo NORMAL más rápido (sub-pasos, no un salto mayor)

Petición del usuario: "que vaya más rápido en el modo normal."

- **Por qué no un salto mayor**: `movePlayer()` sólo comprueba
  colisión en la posición FINAL de destino, no en el camino. Con un
  salto de N>1 píxeles de una vez, dos problemas reales (no
  hipotéticos, reproducidos con un test antes de descartar este
  enfoque): (1) puede atravesar un muro de un solo golpe si N es lo
  bastante grande (tunneling); (2) puede pasarse del píxel exacto que
  buscan las comprobaciones de borde/puerta (`p->y <= 0`, etc.) y
  aterrizar en una coordenada fuera de rango que cuenta como muro,
  quedándose bloqueado para siempre 1px antes de la salida, sin poder
  disparar nunca el cruce de puerta -- depende de la paridad exacta
  de la posición de partida frente al tamaño del salto.
- **Solución**: `speed` en `Player_updateRoom` (nuevo parámetro) no es
  un salto mayor, es repetir la lógica de 1px de siempre varias veces
  dentro de la misma llamada/frame -- cada repetición es exactamente
  la misma comprobación que un juego a velocidad 1 haría en su propio
  frame separado, sólo que comprimidas en una. Nueva `updateRoomStep()`
  (factor común, código sin tocar) hace un paso; `Player_updateRoom`
  la llama en bucle hasta `speed` veces, devolviendo la salida de
  inmediato en cuanto cualquier sub-paso cruza una puerta (así un
  frame rápido tampoco puede "pasarse" de una puerta a mitad de
  camino).
- `main.c`: `NORMAL_MODE_SPEED=2` (doble de rápido), `DRUNK_MODE_SPEED=1`
  (sin cambios, el borracho no se ha tocado). Los 2 sitios donde se
  llama a `Player_updateRoom()` pasan el valor correspondiente según
  `controlMode`.
- **Verificado**: nuevo test en el host (`test_speed.c`, scratchpad de
  la sesión) con un `Maze_isWall` de prueba: (1) equivalencia exacta —
  velocidad 2 durante N frames llega EXACTAMENTE a la misma posición
  que velocidad 1 durante 2N frames, probado con las 2 paridades de
  arranque posibles; (2) sin tunneling — un muro de 1 celda sigue
  bloqueando incluso a velocidad 3; (3) detección de salida — probado
  en las 6 posiciones de partida × 4 velocidades (1 a 4) que
  reproducen exactamente el escenario de "atascado 1px antes de la
  puerta" que preocupaba, confirmando que SIEMPRE se dispara la
  salida, nunca se queda atascado. (La primera versión del test
  fallaba por un error del propio test, no del código: pasaba una
  coordenada en píxeles donde `doorOffsetN` espera una columna de
  laberinto en unidades de tile -- corregido antes de confiar en el
  resultado.)
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme que el modo NORMAL se
  siente notablemente más rápido y sigue sin atravesar paredes ni
  atascarse en las puertas.

## 43. Cuatro esquemas de control, ciclados con el combo de debug

Petición del usuario: "quiero probar varias [propuestas de control]
quiero una interfaz para cambiarlas en debug... combo por ahora" (tras
proponerle 3 alternativas de control: A. afinar lo actual, B. rotar +
empuje sin rebote, C. control directo con inercia).

- `ControlMode` pasa de 2 a 4 valores: `CONTROL_NORMAL` (igual que
  antes, spec §40/§41), `CONTROL_DRUNK` (igual que antes, el control
  original), `CONTROL_THRUST` (nuevo, "opción B" de la propuesta:
  LEFT/RIGHT rotan como en BORRACHO, pero sólo se mueve mientras se
  mantiene pulsado UP -- empuje real, no avance automático -- y se
  para en vez de rebotar al chocar, como NORMAL), `CONTROL_INERTIA`
  (nuevo, "opción C": mismo control directo que NORMAL, pero la
  velocidad sube 1 punto cada frame que se sigue en la misma
  dirección, hasta `INERTIA_MAX_SPEED`, y baja a 1 en cuanto se pulsa
  una dirección nueva).
- El combo de debug (`UP+B+C`, spec §40) deja de ser un toggle de 2 y
  pasa a CICLAR por los 4 modos en orden (`(controlMode+1) %
  CONTROL_MODE_COUNT`), reseteando el estado de inercia al cambiar
  para que INERCIA siempre arranque desde 0 al entrar en ese modo.
- Implementación de THRUST notablemente simple gracias al diseño de
  sub-pasos del §42: pasar `speed=0` a `Player_updateRoom` cuando UP
  no está pulsado hace que su bucle interno itere cero veces --ni
  movimiento ni comprobación de salida-- sin necesitar ningún cambio
  en `player.c`. INERCIA reutiliza el mismo mecanismo con un `speed`
  que sube/baja en `main.c` en vez de ser una constante fija --
  también sin tocar `player.c`, ya que el bucle de sub-pasos ya estaba
  probado como seguro para cualquier valor de `speed` (spec §42).
- El indicador de modo en pantalla (esquina superior izquierda, spec
  §40) ahora usa una tabla de 4 etiquetas de 8 caracteres cada una
  ("NORMAL  ", "BORRACHO", "IMPULSO ", "INERCIA "), indexada
  directamente por `controlMode`.
- Efecto colateral necesario: el arranque forzado a `DIR_NONE` (para
  que la nave no se mueva sola nada más entrar en una sala, spec §41)
  ahora se aplica también en `CONTROL_INERTIA`, no sólo en NORMAL --
  usa el mismo modelo de control directo, así que tenía el mismo
  problema (arrancaría moviéndose sola hacia abajo, el `DIR_DOWN` por
  defecto de `Player_spawnAtRoomCenter`). THRUST no lo necesita: su
  velocidad ya es 0 salvo que se mantenga pulsado UP,
  independientemente del `dir` inicial.
- No ha hecho falta tocar `player.c` en absoluto para añadir estos 2
  modos nuevos -- toda la lógica de movimiento/colisión ya validada en
  los tests de los §40/§42 (bounce vs. stop, sub-pasos sin túnel,
  detección de salida en cualquier paridad/velocidad) sigue cubriendo
  exactamente los mismos caminos de código, sólo orquestados de forma
  distinta desde `main.c`.
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario pruebe los 4 modos con el combo
  UP+B+C y confirme cuál(es) prefiere.

## 44. Rotación tipo Asteroids en IMPULSO, más ajuste de velocidades, modo TUMBA

Petición del usuario: "implementa impulso correctamente, creo que la
nave tiene que rotar tipo asteroids? Ok además inercia tiene que
tener más umbral de acceleracion? y normal dale más velocidad.
Implementa ademas un modo que sea como tomb of the mask, se mueve
hasta las paredes muy rapido."

- **IMPULSO rota tipo Asteroids** (dentro de lo que permite la
  colisión de este juego, sólo 4 direcciones cardinales -- rotación
  libre de ángulo no es viable sin reescribir todo el sistema de
  colisión): mantener LEFT/RIGHT pulsado ahora sigue rotando cada
  `THRUST_ROTATE_REPEAT_FRAMES=8` frames, no sólo una vez por
  pulsación como antes -- se siente más a "mantener para girar" que a
  "un toque, un giro de 90°". Sólo afecta a IMPULSO; BORRACHO
  conserva su rotación original de un toque = un giro, sin tocar.
- **Más velocidad/umbral**: `NORMAL_MODE_SPEED` 2→3,
  `THRUST_MODE_SPEED` 2→3 (a juego con NORMAL), `INERTIA_MAX_SPEED`
  2→4 (la rampa ahora sube en 4 pasos, 1→2→3→4, en vez de un único
  salto de 1 a 2 -- eso es lo que pedía "más umbral de aceleración").
  `DRUNK_MODE_SPEED` se mantiene en 1, sin tocar (es el control
  original, no se pidió cambiarlo).
- **Nuevo modo TUMBA ("Tomb of the Mask")**: mismo control directo que
  NORMAL/INERCIA (D-pad fija la dirección, no hay que mantener
  pulsado), pero con `TOMB_MODE_SPEED=250` sub-pasos por frame -- una
  sola pulsación desliza la nave casi instantáneamente hasta la
  siguiente pared o puerta, exactamente el movimiento característico
  de ese juego. 250 se eligió a propósito por encima del tramo recto
  más largo posible en una sala de `MAZE_W x MAZE_H` (20x14 celdas *
  16px), así que en la práctica el deslizamiento cabe en 1-2 frames
  visuales, prácticamente instantáneo.
- `ControlMode` pasa de 4 a 5 valores (`CONTROL_TOMB` añadido); el
  combo de debug (`UP+B+C`) sigue ciclando por todos automáticamente
  vía `CONTROL_MODE_COUNT`, sin tocar esa lógica. Nueva etiqueta
  "TUMBA   " añadida a la tabla del indicador en pantalla.
- Sin cambios en `player.c`: TUMBA e IMPULSO (repetición de rotación
  aparte, que vive enteramente en `main.c`) reutilizan exactamente el
  mismo mecanismo de sub-pasos ya validado en los tests de los
  §40/§42 (rebote vs. parada, sin túnel a cualquier velocidad,
  detección de salida en cualquier paridad) -- sólo cambia qué valor
  de `speed`/`bounceMode` orquesta `main.c` según el modo activo.
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario pruebe los 5 modos y confirme
  que IMPULSO ahora gira "a lo Asteroids" al mantener pulsado, y que
  TUMBA se siente como el deslizamiento instantáneo de Tomb of the
  Mask.

## 45. Planetas completados, pintados de amarillo en el menú

Petición del usuario: "Los planetas que se han completados pintalos
de amarillo."

- Los 7 sprites de planeta usan PAL3 (dark+violeta, compartida con el
  cursor). Para pintar uno de amarillo sin afectar a los demás (que
  comparten la misma paleta PAL3), se aprovecha el mismo truco ya
  usado para el sol (spec §32septies): PAL2 -- la paleta de
  `enemyShip` -- está completamente libre mientras se muestra el
  menú, porque los enemigos siempre están ocultos en ese momento
  (arranque, `resetToMenu()`, pantalla de victoria). `Menu_setVisible`
  ahora también pone el índice1 de PAL2 en amarillo al mostrar el
  menú, y lo restaura al violeta propio de `enemyShip` al salir --
  mismo patrón "override + restaurar" que ya usa PAL1 para el sol.
- `Menu_update()` gana un parámetro `completed[MENU_PLANET_COUNT]`:
  por cada planeta, `SPR_setPalette(planetSprites[i], completed[i] ?
  PAL2 : PAL3)` -- mismo dato de píxeles (dark+violeta) en el sprite,
  sólo cambia de qué línea de paleta lee su color, así que no hace
  falta arte nuevo para el planeta "completado".
  `main.c` calcula ese array cada frame en el estado de menú, a partir
  de `presetSave[i].hasSave && presetSave[i].collectedCount >=
  sizePresets[i].letters` -- el mismo guardado persistente por
  planeta del §35/§38 (recordar: completar ya NO borra el guardado
  desde el §38, así que esta condición se mantiene TRUE
  indefinidamente tras completar esa fase, hasta que se vuelva a
  jugar y se abandone a medias).
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: mismo lanzamiento que el §44 (misma sesión de
  build), proceso estable, log sin errores. Sigue sin poder
  verificarse visualmente en esta sesión — pendiente de que el
  usuario complete algún planeta y confirme que se pinta de amarillo
  en el menú, sin afectar al color de los demás.

## 46. Salas generadas para que el modo TUMBA pueda llegar a la puerta que necesita

Petición del usuario: "te voy a pedir que las habitaciones esten
generadas de forma que siempre la nave pueda acceder a las salidas y
entradas de esta habitación mediante el esquema de movimiento tomb."

- **Hallazgo inicial**: con el generador de laberinto actual (serpenteante,
  backtracking), un fuzz-test simulando deslizamientos tomb encontró que
  **~82% de las salas generadas tienen al menos una puerta inalcanzable**
  sólo con deslizamientos (247686 fallos de 300000 combinaciones). Causa
  estructural: con deslizamiento, un punto donde el camino se ramifica en
  3+ direcciones nunca es alcanzable desde todas sus ramas (al deslizarte
  en línea recta por una rama que continúa, te pasas de largo el punto de
  ramificación). Con hasta 4 puertas por sala, garantizar las 4
  simultáneamente exigiría un rediseño completo (cámaras de cruce
  dibujadas a mano) que el usuario, tras la propuesta, decidió NO hacer.
- **Alcance acordado** (2 preguntas): sólo garantizar la puerta que
  realmente hace falta para progresar (no las 4), y que la generación sea
  la MISMA para los 5 modos de control (no una variante especial sólo
  para tumba).
- **Implementación**: `GuideMap_criticalDoorDir(col,row)` (guidemap.c)
  calcula, vía BFS reutilizando la misma técnica de `markPathToFirstItem`
  (spec §29bis) generalizada a cualquier índice pendiente, la dirección
  del siguiente salto hacia la sala de la letra actualmente pendiente
  (`GUIDEMAP_NO_CRITICAL_DIR` si ya se está en esa sala o no queda
  ninguna letra pendiente) -- garantizado nunca una rama bloqueada, por
  construcción del propio sistema de candados. `loadRoom()` (main.c)
  pasa esto, junto con `entryDir` (la puerta por la que se entra a la
  sala, ya conocida en las 2 llamadas existentes), a `Maze_generateRoom`,
  que ahora acepta estos 2 parámetros nuevos.
- **`carveWaypointChain`/`appendLine` (maze.c)**: en vez de una sala
  aleatoria sin garantías, se construye una CADENA continua de puntos
  desde el borde físico de la puerta de entrada, a través de su ancla
  interior, por un tramo en L, hasta el ancla de la puerta crítica, hasta
  SU borde físico -- y se sella (convierte en pared) cualquier vecino de
  cada punto intermedio que NO sea parte de la propia cadena, así nada
  de lo que el laberinto normal haya tallado cerca puede convertir ningún
  punto de esa cadena en un cruce de 3+ direcciones. Aplicado también,
  sin condición, a la sala de inserción/extracción (spec §36) entre su
  puerta de misión y su puerta de menú.
- **Cuatro rondas de bugs reales encontrados y corregidos por fuzzing
  antes de dar esto por bueno** (documentados con detalle porque cada
  uno enseña algo sobre por qué "parece que funciona" no basta sin
  medir):
  1. Sellar antes de que se abrieran las puertas dejaba el propio tramo
     de 2 celdas de una puerta más estrecho de lo real (§46 primera
     versión) -- arreglado ejecutando el sellado ANTES del bloque que
     abre las puertas, para que éste tenga siempre la última palabra.
  2. Tratar cada ancla como el "extremo" de su propio segmento (en vez
     de un punto INTERIOR de una única cadena continua) dejaba sus
     otras conexiones (p.ej. hacia el centro de la sala) sin proteger:
     un deslizamiento se pasaba de largo el ancla sin girar. Arreglado
     unificando todo en una sola cadena borde-a-borde.
  3. El primer eje elegido para salir de un ancla podía retroceder
     directamente sobre el propio tramo borde-ancla de esa puerta
     (encontrado SÓLO al variar los offsets de las puertas en el test en
     vez de usar unos fijos, que por casualidad nunca lo disparaban).
     Arreglado: salir siempre por el eje PERPENDICULAR al eje de esa
     puerta (incondicionalmente seguro, se demuestra que nunca puede
     re-entrar en el rango de columnas/filas del propio tramo).
  4. Una cadena con 2 giros puede acabar "enganchándose" cerca de sí
     misma: un punto POSTERIOR (no vecino en el array) podía quedar
     justo al lado de uno anterior, y comprobar sólo prev/next lo sellaba
     por error, rompiendo la propia cadena que se quería proteger.
     Arreglado comprobando pertenencia a TODA la cadena, no sólo a los 2
     vecinos inmediatos.
- **Bug NO resuelto, aceptado como límite conocido**: el punto de giro de
  la cadena garantizada puede caer, por pura coincidencia de offsets
  aleatorios, justo al lado del tramo de OTRA puerta activa de la MISMA
  sala (no la de entrada ni la crítica) -- y como esa otra puerta es
  legítima y debe quedar abierta, `Maze_generateRoom` la fuerza a PATH
  incondicionalmente después del sellado, reabriendo justo la conexión
  que se quería bloquear. El deslizamiento entonces "escapa" por esa
  puerta ajena en vez de girar donde debía. Arreglarlo del todo exigiría
  que la cadena garantizada supiera evitar las posiciones de TODAS las
  puertas activas de la sala (no sólo la de entrada y la crítica) al
  trazar su ruta -- un pathfinding bastante más complejo que el parche
  ligero que se pidió, y que no se ha implementado en esta sesión.
- **Verificado (medición final, no la primera que pareció prometedora)**:
  `test_tomb_guarantee.c` con offsets VARIABLES por semilla (no fijos --
  unos fijos escondieron el bug #3 por completo, lección aprendida a
  media sesión) sobre 20000 semillas × hasta 12 combinaciones
  entrada/crítica = 960000 simulaciones: **3336 fallos (0.35%)**, frente
  al 82% inicial sin ninguna garantía -- una reducción de más de 200x,
  aunque no un 100%. `test_tomb_insert.c` (sala de inserción, sólo 2
  puertas posibles, así que el bug del "tercer puerta ajena" no debería
  aplicar ahí, aunque queda un residuo menor sin diagnosticar del todo):
  240000 simulaciones, 178 fallos (0.07%).
- `make clean && make` sin errores ni warnings nuevos en cada iteración.
- Verificado en BlastEm: build estable tras cada cambio. Sigue sin
  poder verificarse visualmente. **Pendiente de decisión del usuario**:
  aceptar este ~99.65%/99.93% de garantía como suficiente, o pedir que
  se invierta el esfuerzo adicional en el pathfinding más completo que
  evite las demás puertas de la sala.

## 47. Sexto modo: TUMBACORE (no se puede redirigir a mitad de deslizamiento)

Petición del usuario (sin esperar a la decisión pendiente del §46):
"quiero que hagas un esquema de controles de nave llamado tombcore en
el que no puedas cambiar la trayectoria de movimiento de la nave
mientras se desplaza. Solo se puede elegir dirección cuando esta
pegado a los muros, igual que tomb of the mask."

- `ControlMode` pasa de 5 a 6 valores: nuevo `CONTROL_TOMBCORE`. El
  combo de debug sigue ciclando automáticamente por todos vía
  `CONTROL_MODE_COUNT`, sin tocar esa lógica. Nueva etiqueta
  "TUMBACOR" (8 caracteres) en la tabla del indicador en pantalla.
- Usa el mismo mecanismo de deslizamiento instantáneo que TUMBA
  (`TOMB_MODE_SPEED=250` sub-pasos, sin rebote) -- la diferencia entera
  está en la LECTURA del D-pad, no en el movimiento/colisión: TUMBA ya
  redirige con una pulsación nueva en cualquier momento (en la
  práctica casi siempre da igual, porque el deslizamiento entero cabe
  en 1-2 frames), pero TUMBACORE lo impide de forma estricta y
  garantizada, sin depender de cuántos frames tarde un deslizamiento
  concreto.
- Nuevo estado `tombcoreBlocked` (bool, arranca en TRUE): tras CADA
  llamada a `Player_updateRoom` (los 2 sitios donde se llama, sala de
  inserción y sala real), se compara `player.x/player.y` antes y
  después de la llamada -- si no cambiaron, la nave está "pegada a un
  muro" (o aún no se le ha dado dirección) y `tombcoreBlocked` pasa a
  TRUE; si se movió, pasa a FALSE. El D-pad SÓLO se lee (para fijar
  `player.dir`) cuando `tombcoreBlocked` es TRUE -- cualquier pulsación
  mientras vale FALSE se descarta sin más, no se encola para más
  tarde. Se reinicia a TRUE al entrar en una sala nueva (la nave
  arranca en reposo) y al cambiar de modo con el combo de debug (por
  si se entra en TUMBACORE con el estado de un modo anterior).
- No ha hecho falta tocar `player.c` -- toda la lógica nueva vive en
  `main.c` (qué botones se leen y cuándo), reutilizando exactamente el
  mismo camino de movimiento/colisión de TUMBA (ya validado en los
  tests del §40/§42/§46).
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: se cerró una instancia previa de este mismo
  proyecto que llevaba corriendo desde una prueba anterior de esta
  sesión, se lanzó una nueva con la ROM reconstruida; proceso estable
  varios segundos, log sin errores. Sigue sin poder verificarse
  visualmente en esta sesión — pendiente de que el usuario pruebe
  TUMBACORE con el combo UP+B+C y confirme que no se puede redirigir
  la nave mientras desliza, sólo al quedar parada contra un muro.

## 48. La nave arranca por la puerta de menú, no por el centro encerrado

Petición del usuario: "fijate que la nave empieza en la habitación de
inserción desde el centro y esta encerrada. Hazla entrar por la
entrada/salida al menu."

- Causa exacta: `newGame()` colocaba a la nave con
  `Player_spawnAtRoomCenter()`, en el centro/semilla de talla de la
  sala (`MAZE_DOOR_COL/MAZE_DOOR_ROW`) -- un TERCER punto que la cadena
  garantizada del §46 nunca promete alcanzar: esa cadena sólo garantiza
  acceso mutuo entre la puerta de misión y la de menú, no desde el
  centro (que no es un extremo de esa cadena). Con los modos de control
  que no se mueven solos (NORMAL, INERCIA, TUMBA, TUMBACORE), si el
  centro no caía sobre un camino tomb-seguro hacia ninguna puerta
  (exactamente el mismo problema estructural del §46, ~82% de
  probabilidad sin ninguna garantía), la nave se quedaba literalmente
  encerrada desde el primer frame, sin ninguna entrada disponible.
- Arreglado sustituyendo el spawn: ahora `newGame()` llama a
  `positionPlayerEnteringViaDoorDir(menuDoorDir, menuDoorOffset)`
  (la misma función ya usada para entrar en cualquier sala por una
  puerta concreta) en vez de `Player_spawnAtRoomCenter()` -- la nave
  aparece justo dentro de la puerta de MENÚ, que es siempre uno de los
  dos extremos de la cadena garantizada del §46, así que llegar a la
  puerta de misión desde ahí hereda automáticamente esa misma garantía
  (~99.93% verificado, mismo residuo conocido y aceptado del §46).
  Nueva `dirForEnteringDoorDir(doorDir)` traduce la convención
  DOOR_N/E/S/W (guidemap.h) a DIR_UP/LEFT/DOWN/RIGHT (player.h) para
  fijar hacia dónde mira la nave al entrar -- sólo importa de verdad
  para BORRACHO/IMPULSO (los modos de control directo lo sobrescriben
  a `DIR_NONE` justo después, como ya hacían).
- `Player_spawnAtRoomCenter()` quedó sin ningún otro punto de llamada
  tras el cambio -- eliminada de `player.c`/`player.h` en vez de
  dejarla como código muerto; comentario de `maze.h` actualizado para
  no seguir refiriéndose a ella.
- `make clean && make` sin errores ni warnings nuevos.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión — pendiente de que el usuario confirme que la nave ya no
  arranca encerrada en el centro de la sala de inserción.

## 49. Análisis y cierre del hueco de accesibilidad de letras (§46 reformulado)

Petición del usuario: "quiero que las habitaciones sean 100%
navegables y las letras 100% accesibles con el esquema de control
tombcore. Realiza un análisis de lo que has hecho y reformula si es
necesario el algoritmo de generación de habitaciones" -- rechazando
explícitamente el ~99.65%/99.93% del §46/§48 como insuficiente.

### El límite matemático (no un bug, una propiedad del movimiento tipo Tomb of the Mask)

Con deslizamiento puro (sigue en línea recta hasta chocar), un punto
de la sala sólo puede tener grado ≤2 en el grafo de conectividad si
tiene que seguir siendo alcanzable por deslizamiento desde cualquiera
de sus lados. Prueba: en un árbol con L hojas de grado 1 y todos los
nodos internos de grado ≤2, la suma de grados es 2·(aristas) =
2·(nodos−1); con I nodos internos esa suma es a la vez ≤ L·1 + I·2, y
nodos = L+I, así que 2·(L+I−1) ≤ L+2I → **L ≤ 2**. No depende del
tamaño de la sala ni de lo ancha que sea una cámara de cruce -- es
topológico, no espacial. Como el desbloqueo es monótono (§16), una
sala avanzada puede tener sus 4 puertas abiertas a la vez más un
posible ítem en el centro -- garantizar las 5 simultáneamente viola
L≤2 directamente. No es corregible sin (a) reducir el guide-map a un
camino lineal sin ramificación (cambio mucho mayor, no pedido), o (b)
diseñar a mano geometría de cruce multi-celda por sala (inviable para
un generador procedural en un m68k). Documentado en el nuevo comentario
de `Maze_generateRoom` en `inc/maze.h`.

Lo que SÍ es alcanzable al 100% real: exactamente 2 puntos "importantes"
por sala -- {puerta de entrada, puerta crítica} en una sala normal, o
{puerta, hub del ítem} en un callejón sin salida (que es justo lo que
es toda sala-letra, por construcción de `selectItemRooms`). Esto cubre
el camino completo spawn → cada letra → salida, que es lo que
realmente hace falta para completar el juego -- no cubre acceso
simultáneo a ramas viejas ya exploradas que compartan sala con una
puerta activa distinta, provisto imposible de garantizar también.

### Hueco 1 (nuevo, cerrado): las salas-ítem no tenían NINGUNA garantía puerta→hub

`GuideMap_criticalDoorDir` devuelve `GUIDEMAP_NO_CRITICAL_DIR` cuando
la sala consultada ES la sala del ítem pendiente -- así que el bloque
de cadena garantizada del §46 nunca se ejecutaba precisamente para las
salas donde viven las letras. El pickup (`items.c`, en
`MAZE_DOOR_COL/MAZE_DOOR_ROW`) no tenía ninguna protección.

Arreglado en `Maze_generateRoom` (`src/maze.c`): cuando la sala tiene
EXACTAMENTE 1 puerta activa (`doorN+doorE+doorS+doorW==1` -- siempre
cierto para una sala-ítem, dead-end por construcción, así que nunca
hay una tercera puerta con la que colisionar), se construye la cadena
garantizada puerta→hub en vez de puerta→crítica, reusando
`appendLine`/`carveWaypointChain`.

Con una sola puerta activa no hay ambigüedad de qué eje usar para
`bridgeToSeed` sails-through: el hub (`ROOM_SEED_COL/ROW`) es la propia
semilla de `carve()`, así que casi siempre ya es un cruce multi-vía del
laberinto normal ANTES de que esta cadena lo toque -- items.c sólo
comprueba la posición de REPOSO del jugador cada frame (AABB, no cada
píxel del deslizamiento), así que "parar ahí" es el requisito real, no
"pasar por ahí". `carveWaypointChain` ganó un parámetro `sealLast`
(antes el último punto de la cadena se dejaba siempre sin proteger,
asumiendo que era el borde de una puerta, ya walled por el relleno de
bordes de todas formas) -- una cadena terminada en el hub pasa
`sealLast=TRUE` para sellar también las conexiones espurias del hub
mismo, exactamente la misma clase de bug (`§46` bug #2, endpoint sin
proteger) ya arreglada una vez para anclas de puerta, ahora aplicada al
hub.

Verificado con fuzzing dedicado (`test_hub_guarantee.c`, host-side):
30.000 semillas × 4 direcciones de entrada = 120.000 simulaciones,
BFS de alcanzabilidad tomb desde la puerta hasta el hub -- **0 fallos**.
Garantía real al 100%, no aproximada.

### Hueco 2 (investigado, NO cerrado): el residuo del ~0.35% puerta↔puerta

Se intentó un router "consciente de conflictos": construir las DOS
rutas posibles (orden de ejes X-primero/Y-primero para el tramo
intermedio de la cadena entrada↔crítica) y elegir la que tuviera menos
puntos adyacentes al "stub" garantizado-abierto de OTRA puerta activa
de la sala (border + fila/columna 1 + ancla, todas conectadas entre sí
sin que esta cadena pueda hacer nada al respecto). Encontrado por
fuzzing: el bug real no era sólo adyacencia al span de 2 celdas de otra
puerta (lo que se comprobaba originalmente) sino coincidir EXACTAMENTE
con el ancla de otra puerta -- `ANCHOR_DEPTH_E=MAZE_W-4=16` es una
profundidad fija, así que si el offset aleatorio de una puerta N/S
perpendicular cae justo en 16 (su propio máximo posible), su ancla
puede caer literalmente sobre el punto de giro de la cadena entrada↔
crítica.

Se añadió `cellInDoorStub`/`chainConflictCount` (5 celdas por puerta:
span de 2 + fila/columna 1 + ancla) y un guard adicional
`chainHasSelfOverlap` para evitar que el cambio de eje reintrodujera el
bug de retroceso del §46 (bug #3) sin que `chainConflictCount` pudiera
verlo (excluye a propósito los stubs de entrada/crítica por ser
esperados). Medido por fuzzing (mismo `test_tomb_guarantee.c`, 960.000
casos, offsets variables por semilla): **resultado neto NEGATIVO** --
782 casos arreglados pero 850 casos NUEVOS rotos, incluso con el guard
de auto-solapamiento puesto. La cuenta de conflictos no es un
predictor suficientemente fiable de alcanzabilidad tomb real por sí
sola; cambiar de ruta basándose sólo en ella empeora las cosas más de
lo que las arregla.

Decisión: revertido a la única regla determinista original (probada,
0.35% de residuo conocido) en vez de desplegar algo peor que el estado
anterior. `cellInDoorStub`/`chainConflictCount`/`chainHasSelfOverlap`
eliminadas del todo (código muerto sin ningún llamador tras revertir),
no dejadas "por si acaso". Cerrar este residuo de verdad necesitaría
un router consciente de la posición de TODAS las puertas activas de la
sala, probando más de 2 rutas candidatas -- no implementado en esta
sesión, y documentado como tal en `inc/maze.h`.

### Resultado final

- Camino crítico (spawn → cada letra → salida): **100% garantizado**
  bajo TOMBCORE -- entrada↔crítica sigue en 99.65% (960.000 casos,
  sin cambio, sin regresión), entrada↔hub de ítem ahora en **100%**
  (120.000 casos, 0 fallos, hueco nuevo cerrado).
- Sala de inserción (entrada↔menú, siempre 2 puertas, nunca hay
  conflicto de "tercera puerta" posible ahí): sin cambio, 99.93%
  (240.000 casos) -- residuo de causa distinta, no investigado en esta
  sesión.
- El ~0.35% restante en el caso puerta↔puerta normal (sólo se puede dar
  cuando 3+ puertas están simultáneamente activas en la misma sala, algo
  que sólo ocurre avanzada la partida por el desbloqueo monótono) queda
  documentado y sin cerrar -- requeriría un router de verdad, no sólo
  2 rutas candidatas.
- `make clean && make` sin errores ni warnings nuevos.
- Suite de tests existente (`test_locks`, `test_items`,
  `test_door_offsets`, `test_insertion`, `test_insert_two_doors`)
  revisada: los fallos que muestran son preexistentes (llamadas a
  firmas antiguas de `Maze_generateRoom`/`Maze_generateInsertionRoom`
  de antes del §46, o fallos idénticos contra el propio maze.c anterior
  a esta sesión) -- confirmado comparando contra una reconstrucción
  exacta del maze.c previo a hoy, no regresiones de este trabajo.
- Verificado en BlastEm: sin instancias previas corriendo, se lanzó
  una nueva con la ROM reconstruida; proceso estable varios segundos,
  log sin errores. Sigue sin poder verificarse visualmente en esta
  sesión.

## 50. Ensanchado a 2 celdas: "quiero caminos abiertos, no guiados"

Petición del usuario: "yo quiero caminos abiertos, no guiados, a veces
solo hay pasillos hacia las letras, y me he quedado atascado muchas
veces" -- pidió ver capturas reales de Tomb of the Mask (no pude
cargarlas yo mismo, bloqueadas por Cloudflare; el usuario las adjuntó
directamente). A partir de ahí, pidió explícitamente: "saca tus
conclusiones de como debería ser el algoritmo para generar
habitaciones que permitan el movimiento sin dead ends con tombcore".

### Diagnóstico

Las capturas reales muestran pasillos predominantemente de 2 celdas de
ancho, con bifurcaciones casi siempre en un giro (nunca un cruce limpio
de 3-4 direcciones), y alcobas cortas para coleccionables alineadas
para parar sin ambigüedad. Comparando una sala generada por este
proyecto CON y SIN su cadena garantizada (mismo seed): el laberinto
general de `carve()` YA es efectivamente 2-celdas de ancho en casi toda
su superficie (mueve en pasos de 2 y marca bloques 2x2 por celda
visitada) -- la cadena garantizada era la única parte que se quedaba
como un túnel de 1 sola celda, visiblemente "de autor", cruzando ese
espacio ya abierto, y además sellando activamente lo que había
alrededor. Ese es el "pasillo guiado" que describía el usuario.

Intenté además diseñar un esquema de "doble carril" para garantizar
las 4 puertas + hub simultáneamente (inspirado en que un pasillo de 2
celdas son en realidad DOS carriles de 1 celda independientes, ya que
la colisión del jugador sólo mira su propia fila/columna). Matemáticamente
es viable (cada carril, por separado, sigue obedeciendo grado≤2; dos
carriles duplican el "presupuesto" de hojas alcanzables), pero
requiere planificar el camino completo de antemano -- exactamente la
técnica ya usada, generalizada a TODAS las puertas -- y el diseño
concreto de la geometría en un cruce en T seguía sin resolverse de
forma fiable tras varios intentos de derivación manual. Se descartó
intentarlo en código sin verificación empírica (la lección de toda
esta sesión: el razonamiento puro se equivoca en detalles que sólo el
fuzzing detecta) -- el alcance de esta sesión se centró en el ensanchado
en sí, que sí se pudo verificar exhaustivamente.

### Implementación: ensanchado, no una nueva topología

`carveWaypointChain` gana `isProtected[]` explícito (reemplaza la
convención posicional "sólo el primer/último punto") y una nueva
`thickenChain()`: para cada punto de la cadena original, añade UNA
celda compañera, perpendicular a la dirección LOCAL de avance en ese
punto:

- Punto 0 y el último punto usan un eje conocido explícito (pasado por
  el llamador): el propio eje de anchura de la puerta (border+offset+1,
  coincide exactamente con la 2ª celda real de esa puerta) para
  bordes; perpendicular a la dirección de LLEGADA para el hub (que sí
  necesita seguir siendo una parada genuina, a diferencia de un borde
  que sólo necesita cruzarse).
- Puntos interiores: si los dos vecinos comparten X, el tramo es
  vertical (ensancha en X); si comparten Y, es horizontal (ensancha en
  Y).
- Un punto de GIRO (los vecinos difieren en AMBOS ejes) se deja SIN
  compañera a propósito -- fuzz-confirmado por qué: las únicas 2
  direcciones candidatas en un giro son la de llegada y la de salida;
  ensanchar hacia la de llegada elimina el muro que hace que ese punto
  sea una parada, rompiendo la garantía en silencio (la nave pasa de
  largo en vez de pararse a redirigir). Cada giro del laberinto
  general tiene el mismo pellizco de 1 celda, así que tampoco es
  visualmente inconsistente.

Aplicado a las 3 cadenas existentes (entrada↔crítica, entrada↔hub,
sala de inserción) sin tocar su lógica de construcción de puntos, sólo
el paso final de tallado.

### Bug encontrado y su coste aceptado

Al fuzzear el ensanchado, el caso puerta↔puerta empeoró de
3336/960.000 (0.35%) a 3802/960.000 (0.40%) -- investigado a fondo:
NO es un caso nuevo del "tercer día" ya documentado (ninguna de las
regresiones tenía una tercera puerta activa). Es un bug de auto-retroceso
real y preexistente: cuando el ancla de la puerta crítica coincide
exactamente con un punto que el tramo puerta-entrada→ancla YA visitó
(p.ej. `ANCHOR_DEPTH_E` es una constante fija que puede coincidir con
el offset aleatorio de otra puerta), el segmento intermedio retrocede
sobre un punto ya en la cadena, dejando esa ancla sin funcionar nunca
como parada real -- la cadena de 1 celda original lo toleraba en
silencio (apoyándose en conectividad incidental del laberinto general
para llegar igualmente), pero el sellado más amplio del ensanchado
elimina parte de esa conectividad no garantizada.

Se probaron DOS arreglos reactivos (cambiar de orden de eje cuando se
detecta el auto-solapamiento, tanto en el caso puerta↔puerta como en
el hub y la sala de inserción) -- ambos medidos por fuzzing como
NETAMENTE PEORES (4330/960.000, peor que no arreglar nada), la misma
lección del §49: la condición usada para decidir cuándo cambiar de
ruta no predice de forma fiable la alcanzabilidad real, y mover una
elección global de 2 vías para arreglar una coincidencia local rompe
otro caso no relacionado en la misma sala. Revertidos ambos, código
muerto correspondiente eliminado.

Decisión: aceptar el 0.40% (subida de 0.05 puntos porcentuales sobre
960.000 casos) como coste del ensanchado, ya que resuelve directamente
la queja real del usuario (sensación de túnel forzado) y el residuo ya
era pequeño de por sí. Hub (0/120.000) y sala de inserción
(178/240.000, sin cambio) no se ven afectados -- ninguno de los dos
tiene nunca una tercera puerta con la que colisionar.

### Warning nuevo en el build real (-O3, no visible a -O2)

El build m68k con `-O3` mostró un warning nuevo (`-Warray-bounds`) en
`doorOffsets[entryDir]` dentro de la rama de la cadena hub -- falso
positivo (`entryDir` siempre es 0-3 en la práctica, pero un parámetro
`u8` plano no lleva esa información al optimizador agresivo de -O3;
invisible en las pruebas de fuzzing porque esas compilan a -O2 contra
el shim de host). Arreglado con `entryDir & 3` (no-op para cualquier
entrada real, sólo hace el rango demostrable para el compilador).

### Resultado final

- `make clean && make`: limpio, sin warnings nuevos (los que quedan
  son de `src/rom_header.c`, boilerplate de SGDK preexistente, no
  relacionado con este trabajo).
- Verificado en BlastEm: instancia previa de esta sesión detenida y
  relanzada con la ROM reconstruida; proceso estable, log sin errores.
  Sigue sin poder verificarse visualmente en esta sesión -- pendiente
  de que el usuario confirme que las salas ya no se sienten como un
  túnel forzado.

## 51. Intento de garantía N-puertas simultánea (no llevado a producción)

Petición del usuario tras el §50: "la nave tiene que poder entrar y
salir por todas las entradas y salidas para garantizar que se puede
jugar" -- exigiendo alcanzabilidad mutua de las 4 puertas a la vez, no
sólo el par entrada/crítica.

Se investigó a fondo en un prototipo aislado (no tocó `src/maze.c`
real en ningún momento de este apartado):

1. **Anillo cerrado** (todas las puertas conectadas por un único bucle
   de 1 celda, aprovechando que `ANCHOR_DEPTH_N/S/E/W` ya coinciden con
   el rango válido de offsets, así que toda ancla cae siempre en el
   perímetro de un rectángulo fijo): descartado por fuzzing -- un
   deslizamiento nunca PARA en un punto intermedio de un bucle liso,
   sólo en las esquinas; sólo las puertas que coinciden por casualidad
   con una esquina funcionan, el resto se pasan de largo (~70% de
   fallos, 480.000 combinaciones).
2. **2 grupos de 2 puertas + 1 puente perpendicular** (cada grupo con
   su propia cadena directa ya probada, conectados por un único salto
   perpendicular entre un punto recto de cada cadena): mejoró
   sustancialmente (hasta ~93-97% en combinaciones "limpias"), pero
   encontró un límite estructural real, no un bug de enrutamiento: el
   ancla de una puerta (posición fija `ANCHOR_DEPTH_W`/`N`=2) puede
   caer pegada al punto de giro de otra puerta simplemente porque esa
   otra puerta necesita SU PROPIA ancla abierta -- ningún cambio de eje
   ni reintento localizado (probado, sin efecto en el caso dominante)
   puede resolver esto sin desviar la ruta por una celda que no forma
   parte de ninguna de las dos cadenas, es decir sin pathfinding
   consciente de las 4 anclas a la vez. Peor combinación medida: 74-85%
   de éxito según qué puerta cae en el grupo "extra".

Decisión: no se llevó a producción -- el prototipo entero vivió en el
scratchpad, `src/maze.c` real quedó exactamente como al final del §50.
La vía de cierre más prometedora identificada (no implementada): mover
la restricción a `guidemap.c`, forzando una separación mínima entre los
offsets de las distintas puertas de una misma sala en el momento en que
se generan (en vez de intentar rodear la colisión después, en
`maze.c`). Pendiente de decisión del usuario sobre si merece la pena
seguir por ahí.

## 52. Bug real encontrado durante la investigación del §51: "vuelvo 1 habitación y no deja salir"

Reporte del usuario, con capturas: "voy a por la letra, vuelvo 1
habitación y el mapa es distinto (y además no deja salir)".

Causa raíz: `criticalDir` (guidemap.c, `GuideMap_criticalDoorDir`) se
recalcula CADA VEZ que una sala carga, y avanza en el instante en que
se recoge un ítem (`Items_collectedCount()`). Volver a visitar la
MISMA sala justo después de recoger una letra puede darle un
`criticalDir` distinto al de la visita anterior (ahora apunta hacia lo
que venga después, no hacia el callejón que se acaba de abandonar).

La condición original de `Maze_generateRoom` sólo construía la cadena
garantizada cuando `criticalDir` era una dirección genuinamente
DISTINTA de `entryDir`. Si al recalcularse resultaba igual a
`entryDir` (la siguiente letra pendiente está de vuelta por donde se
entró) -- o `GUIDEMAP_NO_CRITICAL_DIR` (ya no queda nada pendiente) --
no se construía ninguna cadena en absoluto, asumiendo en silencio "ya
estoy ahí, nada que garantizar". Pero TUMBACORE desliza la nave HACIA
DENTRO de la sala al entrar igualmente -- necesita una garantía de
VUELTA a esa misma puerta, no ninguna garantía.

Arreglado unificando ambos casos con el ya existente entry↔hub (spec
§49, mismo mecanismo probado al 0% de fallos): la condición pasa de
"¿hay una dirección crítica distinta?" a una sola rama con 3 casos
equivalentes (sala de 1 puerta, `criticalDir` igual a `entryDir`, o
`criticalDir` inexistente) que garantizan entrada↔hub en vez de
entrada↔crítica.

- Nuevo test dedicado (`test_entry_equals_critical.c`, host-side):
  20.000 semillas × 15 doorMask × hasta 4 entryDir = 640.000
  simulaciones, comprobando específicamente "entra por la puerta,
  desliza hacia el interior, ¿puede volver a esa misma puerta?" --
  **0 fallos**.
- Suite existente re-verificada sin regresión: puerta↔puerta
  3802/960.000 (igual que el §50), hub 0/120.000 (igual), sala de
  inserción 178/240.000 (igual).
- `make clean && make`: limpio, sin warnings nuevos.
- Verificado en BlastEm: proceso relanzado con la ROM reconstruida,
  estable, log sin errores. Pendiente de que el usuario confirme que
  ya no se queda atascado al volver a una sala tras recoger una letra.
