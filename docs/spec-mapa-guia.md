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
