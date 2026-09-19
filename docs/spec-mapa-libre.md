# Spec: Mapa Libre (reemplaza el modelo de progreso bloqueado de spec-mapa-guia.md)

Petición del usuario, tras una larga serie de parches sobre el modelo
anterior (locks, camino crítico, cadena garantizada dinámica por
visita -- ver `docs/spec-mapa-guia.md` §1-52 para el historial
completo, que este documento sustituye en su diseño de fondo, no en su
valor como registro de lo ya probado):

> "si, refactoriza, deja de parchear. Haz unos specs nuevos para el
> mapa. Generas primero el mapa y después dejas a la nave circular en
> el en busca de las letras. elimina los enemigos."

## 1. Por qué el modelo anterior no se podía seguir parcheando

El modelo anterior generaba el interior de cada sala **en el momento
de visitarla**, con una garantía de alcanzabilidad que dependía de
`entryDir`/`criticalDir` -- ambos recalculados cada vez, y
`criticalDir` cambia en el instante en que se recoge una letra. Esto
tenía dos problemas de fondo, no arreglables con más parches locales:

1. **La sala podía cambiar de forma entre dos visitas** (bug
   reportado por el usuario, spec §52): si `criticalDir` apuntaba a
   una puerta distinta en la segunda visita, la cadena garantizada se
   reconstruía con otra forma, y la ruta que había funcionado la vez
   anterior podía dejar de estar protegida.
2. **Garantizar las 4 puertas a la vez con una sola pasada de
   enrutamiento es estructuralmente difícil** (spec §51): el ancla de
   una puerta puede caer pegada al punto de giro de otra por pura
   coincidencia geométrica, y no hay margen para desviar la ruta sin
   pathfinding consciente de las demás anclas -- cualquier arreglo
   local movía el problema a otro sitio.

La generación por visita, en tiempo real, no permite un tercer
recurso obvio para el punto 2: **reintentar con otra semilla si la
comprobación falla**. Cambiar de arquitectura a "genera todo el mapa
una vez, al empezar la partida" abre esa puerta: la generación ya no
tiene que ser instantánea sala-por-sala durante el juego, así que
puede permitirse verificar cada sala por BFS real y, si falla,
probar la siguiente semilla -- sin necesidad de que el algoritmo de
una sola pasada sea perfecto, sólo razonablemente bueno.

## 2. Modelo nuevo, en una frase

**El mapa completo (topología + cada sala interior + posiciones de
letras) se genera y verifica UNA VEZ al pulsar "empezar", con un
reintento automático y acotado por sala si la primera semilla no
garantiza que todas sus puertas activas sean mutuamente alcanzables
deslizando; a partir de ahí, la nave circula libremente por un mapa
ya fijo, sin bloqueos de progreso, buscando las letras en cualquier
orden.**

## 3. Qué se elimina

- **Candados / camino crítico** (spec §16 del documento anterior):
  `GuideMap_recomputeLocks`, `GuideMap_isRoomLocked`,
  `GuideMap_criticalDoorDir`, `Items_isUnlocked`, y el parámetro
  `lockedN/E/S/W` de `Maze_generateRoom`. Ninguna puerta se sella
  nunca -- todas las del árbol están siempre abiertas desde el primer
  frame.
- **Orden de recogida de letras**: `Items_tryCollect` ya no exige que
  sea "la letra que toca" -- recoge cualquier letra no recogida que
  la nave toque, en cualquier orden.
- **`goalCol`/`goalRow`** (spec §4.3 del documento anterior): nunca
  llegó a leerse fuera de `guidemap.c`, código muerto -- eliminado
  junto con `selectGoal()`.
- **La restricción "camino hacia la letra A" de `selectInsertionLink`**
  (spec §29bis): existía sólo para evitar que el enlace de inserción
  cayera en una sala que los candados pudieran sellar. Sin candados,
  CUALQUIER sala del árbol es alcanzable siempre desde el inicio, así
  que la restricción ya no hace falta -- cualquier sala con un lado
  libre vale.
- **Enemigos**: `enemy.c`/`enemy.h` eliminados del todo, junto con
  todo su uso en `main.c` (spawneo, patrulla, colisión enemigo-
  enemigo, sprites).
- **`entryDir`/`criticalDir` como parámetros de `Maze_generateRoom`**:
  ya no existen -- la garantía de una sala es una propiedad ESTÁTICA
  de su semilla (ver §5), no algo que dependa de por dónde entras ni
  de qué letra falte.

## 4. Qué se mantiene (sin cambios de fondo)

- El árbol de salas por Prim's aleatorio (`carveTree`), la poda de
  hojas (`pruneLeaves`), el color por sección (`computeSections`), y
  la selección de salas-letra por muestreo de punto más lejano
  (`selectItemRooms`) -- todo esto es estructura/estética, no depende
  de candados ni de orden.
- El ensanchado a 2 celdas de los pasillos (spec §50) y la técnica de
  cadena garantizada punto-a-punto (`appendLine`/`carveWaypointChain`/
  `thickenChain`, spec §46-50) -- se siguen usando como el mecanismo
  de bajo nivel, sólo que ahora construyen "todas las puertas activas
  mutuamente alcanzables" en vez de "entrada↔crítica".
- La sala de inserción (dos puertas fijas, misión + menú) -- sigue
  igual, ahora también con reintento (ver §5).

## 5. Generación con verificación y reintento (el mecanismo nuevo)

Cada sala (y la sala de inserción) se genera así:

1. Tallar el interior con la técnica de 2 grupos + puente (spec §51):
   hasta 4 puertas activas se dividen en 2 grupos de máximo 2; cada
   grupo obtiene su propia cadena directa puerta↔puerta (o
   puerta↔hub si sólo hay 1 puerta en la sala, spec §49); si hay
   2 grupos, se conectan con un único salto perpendicular entre un
   punto recto de cada cadena.
2. **Verificar por BFS real** (mismo modelo de deslizamiento tomb que
   ya se usaba en los tests de fuzzing de todo el resto de esta
   sesión, ahora compilado dentro del juego): ¿son mutuamente
   alcanzables por deslizamiento TODAS las puertas activas entre sí
   (y el hub, si la sala tiene una letra)?
3. Si falla, **reintentar** hasta `MAZE_MAX_SEED_ATTEMPTS`=8 veces,
   variando DOS cosas a la vez, no sólo la semilla: reintentar sólo
   `roomSeed+intento` (dejando fija la agrupación de puertas) resultó
   NO ayudar nada al tipo de fallo dominante -- medido en un caso real,
   los 8 intentos fallaron de forma idéntica, porque esa clase de
   fallo viene de posiciones fijas (`ANCHOR_DEPTH_*`) coincidiendo con
   el offset aleatorio de otra puerta, algo que la semilla de `carve()`
   no cambia en absoluto. Por eso cada intento TAMBIÉN rota qué puerta
   empieza el reparto grupo1/grupo2 (4 rotaciones posibles). Con ambas
   variando, el fallo medido (960.000 pares puerta-a-puerta simulados)
   bajó de ~22% a ~4.2% -- una mejora real, pero lejos de
   despreciable; ver §9 para el estado honesto de este residuo.
4. El intento que gana (0 si el primero ya vale, casi siempre) se
   guarda en un campo nuevo de 3 bits de `MapCell`
   (`seedAttempt`) para que revisitar la sala reproduzca EXACTAMENTE
   la misma sala, siempre -- ya no hay `entryDir`/`criticalDir` que
   puedan cambiar la forma entre visitas, así que esto por fin es
   una garantía real de estabilidad, no sólo determinismo de la
   semilla base.

Este paso 2-3 corre una vez por sala durante la pantalla de "generando
mapa..." al pulsar "empezar" (o al reanudar una partida guardada) --
nunca durante el juego en tiempo real, así que el coste de los
reintentos (raros, y cada uno es sólo tallar+comprobar una rejilla de
20x14) es completamente invisible para el jugador.

## 6. Letras y objetivo, sin orden

- Las hasta 5 letras (`itemCount`, sin cambios) siguen colocadas en
  salas sin salida repartidas por muestreo de punto más lejano.
- Se pueden recoger en cualquier orden -- tocarlas basta,
  independientemente de cuántas otras queden.
- El mapa (`GuideMap_drawOverlay`) muestra siempre todas las letras
  ya descubiertas (visitadas) y las 5 salas-letra con su marco hueco
  si aún no se han visitado (como antes, spec §21) -- pero ya no hay
  concepto de "la letra que toca ahora", así que no hay ocultamiento
  por orden, sólo por fog-of-war de visitado/no visitado.
- Completar la fase sigue siendo: recoger las 5 (o las que tenga el
  preset) letras Y salir por la puerta de menú de la sala de
  inserción (spec §39 del documento anterior, sin cambios).

## 7. Guardado por planeta, sin asumir orden

El progreso guardado por planeta (`presetSave`, spec §35 del
documento anterior) asumía que "N recogidas" implicaba siempre "las
primeras N, en orden" -- ya no es cierto sin orden forzado. Se
sustituye `collectedCount` (un número) por `collectedMask` (un
bitmask de qué letras concretas están recogidas) -- mismo mecanismo
de guardado, capaz de representar cualquier subconjunto.

## 8. Enemigos: eliminados, no aparcados

`enemy.c`/`enemy.h` se eliminan del repositorio, no se dejan sin usar
-- convención ya establecida en este proyecto de no dejar código
muerto. Si en el futuro se quiere reintroducir algún tipo de
obstáculo, será un diseño nuevo, no una reactivación de éste (su
propio pathing fijo por eje no tenía en cuenta el nuevo sistema de
pasillos anchos de todos modos).

## 9. Qué NO resuelve esto (honestidad, no todo lo pendiente desaparece)

- **El residuo medido es ~4.2%, no indistinguible de 0** (960.000
  pares puerta-a-puerta simulados, host-side). Reintentar SÓLO la
  semilla de `carve()` no ayuda al tipo de fallo dominante (posiciones
  de ancla fijas coincidiendo con el offset aleatorio de otra puerta,
  algo que no depende de esa semilla en absoluto -- confirmado: 8/8
  intentos idénticos en un caso real, antes de añadir la rotación de
  agrupación). Con la rotación añadida, bajó de ~22% a ~4.2% -- una
  mejora real, pero lejos del "prácticamente 0" que se esperaba al
  diseñar el mecanismo. Cerrarlo de verdad necesitaría o bien el
  pathfinding consciente de las 4 anclas que spec §51 ya dejó
  pendiente, o bien estrechar el rango de offsets de puerta en
  `guidemap.c` (hecho parcialmente: `DOOR_COL/ROW_MIN/MAX` ya se
  estrecharon 1 celda a cada lado para evitar la coincidencia EXACTA
  con `ANCHOR_DEPTH_*`, pero las coincidencias por proximidad, no sólo
  exactas, siguen sin resolverse).
- **No verificado con juego real en esta sesión**: el build m68k
  compila limpio y BlastEm arranca estable, pero nadie ha pulsado
  "empezar" y jugado una partida completa todavía -- ni el tiempo real
  de la pasada `GuideMap_verifyAllRooms` (hasta ~80 salas en el preset
  más grande, cada una con hasta 8 intentos de talla+verificación) ni
  la sensación de juego (control tombcore, recogida libre de letras,
  ausencia de enemigos) se han confirmado visualmente. Pendiente de
  que el usuario lo pruebe.
- Si el tiempo de generación upfront resulta perceptible en hardware
  real, la pantalla de "generando mapa..." debería comunicarlo (no
  quedarse en negro sin explicación) -- no implementado en esta
  sesión; `newGame()` simplemente bloquea hasta que
  `GuideMap_verifyAllRooms` termina, sin ningún indicador visual
  mientras tanto.
