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
