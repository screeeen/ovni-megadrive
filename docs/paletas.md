# Las paletas del juego

El VDP tiene cuatro paletas de 16 colores (64 entradas de CRAM) y **ocho niveles
por componente**: `00 24 48 6D 91 B6 DA FF`. `RGB24_TO_VDPCOLOR` redondea a esa
rejilla, asi que un color fuera de ella se mueve solo; los valores de este
documento ya estan dentro. `RGB3_3_3_TO_VDPCOLOR(r,g,b)` toma esos niveles
directamente (0..7) y es la forma de no equivocarse.

El indice 0 de cada paleta es TRANSPARENTE en sprites; en un plano de fondo es
un color normal, y el 0 de PAL0 es el fondo de todo el juego.

**La tinta de todo texto es el indice 15.** Las dos fuentes lo llevan grabado en
el arte: la de SGDK y la del HUD (`tools/font_from_bin.py`, `INK_INDEX = 15`).
Recolorear texto por el indice 1 no hace nada.

El juego tiene dos regimenes que NO coexisten -- **menu** y **partida** -- y el
menu pide prestadas entradas de las cuatro paletas, se las guarda al entrar y
las devuelve al salir (`Menu_setVisible`). Por eso cada tabla lleva las dos
columnas.

---

## PAL0 — el escenario, el texto y el HUD

El plano BG_A (laberinto), BG_B (HUD y textos), las plantas y los iconos de
letra. Es la unica paleta que vive a la vez en fondo y en sprite
(`itemIconSprite`).

| idx | en partida | en el menu | quien lo escribe |
|----|------------|------------|------------------|
| 0 | fondo del juego, `MAZE_BG_COLOR` = `0x000024` | igual | `Maze_loadGraphics` |
| 1 | `WALL_COLOR_RGB` = `0x987DFA` (ver AVISO 1) | gris de planeta cerrado, `LOCKED_PLANET_COLOR` | `Maze_loadGraphics` / `menu.c` |
| 2 | **suelo** del planeta, `PAL_SUELO` | violeta de la barra (`BAR_INK_INDEX`) y nivel 0 de la rampa gris | `Maze_setPlanetTint` / `menu.c` |
| 3 | **cuerpo** del muro, `PAL_CUERPO` | rampa gris | `Maze_setPlanetTint` |
| 4 | **sombra** del muro (los huecos de la trama), `PAL_SOMBRA` | rampa gris | `Maze_setPlanetTint` |
| 5 | **brillo**, `PAL_BRILLO` — uno solo para todo el juego | rampa gris | `Maze_setPlanetTint` |
| 6..9 | libres | rampa gris del planeta cerrado | `menu.c` |
| 10 | negro del recuadro de depuracion, `DEBUG_BOX_PAL_INDEX` | rampa gris | `Maze_loadGraphics` |
| 11 | planta oscura, `#490049` | rampa gris | `Plants_loadGraphics` |
| 12 | planta media, `#B64992` | rampa gris | `Plants_loadGraphics` |
| 13 | planta clara, `#FFB6DB` | rampa gris | `Plants_loadGraphics` |
| 14 | silueta de planta no recogida, `#6D2449` | libre | `Plants_loadGraphics` |
| 15 | **tinta de texto**, blanco | igual | la fuente |

**Las paletas son elegibles** (`MAZE_PALETTES`, `Maze_setPalette`): la lista es
AUTO -- la del planeta, el comportamiento de siempre -- mas las ocho de los
planetas y tres ganadas con la barra de progreso (PHOSPHOR, NEON, MONO). Elegir
una manda sobre el planeta en todas las salas. Los colores y sus nombres viven
juntos en `mazePalette[]` / `mazePaletteName[]` (`src/maze.c`) para que no
puedan descuadrarse, y quien tiene cual lo decide `paletaOwned()` en main.c: la
de un planeta se gana limpiandolo.

Las cuatro entradas del escenario (2..5) son lo UNICO que cambia por planeta:
tintar es mover cuatro colores, no cambiar arte, asi que ocho planetas cuestan
lo mismo que uno. La tabla esta en `planetTint[]` (`src/maze.c`). Lo que separa
muro de suelo es el **valor**, no el tono — en los cuatro primeros planetas a
todo contraste (trama al escalon mas alto, suelo a negro) y de ahi en adelante
con la sombra igualada al suelo.

Indices 2..13 tienen tres dueños que se reparten el tiempo (escenario, plantas,
rampa del menu). El menu guarda y restaura 2..13 (`savedPlanetInk`), y la
pantalla de coleccion guarda y restaura el 2 (`collSavedFloor`).

## PAL1 — la nave

Los 16 colores de `playerShip` tal cual vienen del recurso
(`PAL_setPalette(PAL1, ...)`), mas:

| idx | en partida | en el menu |
|----|------------|------------|
| 1 | amarillo de la nave, `PLAYER_SHIP_COLOR` = `0xFFFF00`; **blanco** mientras parpadea en el mapa (`PLAYER_SHIP_INK_INDEX`) | tinta del sol (`SUN_INK_INDEX`) |
| 15 | tinta de texto del recurso | nombre de planeta cerrado (`NAME_PAL_LOCKED`) |

Sprites: `playerSprite`, `mapShipSprite`.

## PAL2 — los enemigos

Los 16 colores de `enemyShip` del recurso, mas:

| idx | en partida | en el menu |
|----|------------|------------|
| 1 | violeta del enemigo (del recurso) | amarillo de planeta terminado (`COMPLETED_INK_INDEX`) |
| 15 | mensaje de vuelta al menu (`RETURN_MSG_INK_INDEX`) | nombre de planeta terminado (`NAME_PAL_DONE`) |

Sprites: `enemySprites[]`, y en el menu los planetas ya terminados
(`SPR_setPalette(..., PAL2)`).

## PAL3 — los planetas del menu, prestada a la partida

Es la paleta ocupada mientras el menu esta a la vista (`planetSmall`, los
sprites de planeta y el cursor) y **libre durante la partida**, que es cuando
`maze.c` la usa para las puertas selladas: misma trama de muro, otra paleta.

| idx | en partida | en el menu |
|----|------------|------------|
| 0 | fondo del laberinto (`LOCKED_DOOR_BG_INDEX`) | `planetSmall[0]` |
| 1 | amarillo `0xFFFF00` (`LOCKED_DOOR_INK_INDEX`) (ver AVISO 2) | `planetSmall[1]` |
| 2..13 | `planetSmall` sin tocar | rampa de tono de la galaxia |
| 15 | `planetSmall[15]`; la pantalla de muerte lo pone blanco para `YOU ARE DEAD` | nombre de planeta abierto (`NAME_PAL_OPEN`) |

---

## Avisos: tres cosas que el codigo dice y no son verdad

1. **`maze.c` cree que la tinta del texto es el indice 1.** El comentario de
   `WALL_TEXT_INK_INDEX` dice "the shared default every text draw in the game
   uses". No lo es: las dos fuentes pintan con el 15. Consecuencia:
   `Maze_restoreTextColor()` escribe `WALL_COLOR_RGB` en PAL0[1] y eso no
   cambia ningun texto. El indice 1 solo es el violeta que el menu pisa para
   los planetas cerrados.

2. **La letra tras puerta cerrada no sale amarilla.** `Maze_setTextColorLocked()`
   se limita a cambiar a PAL3, y da por hecho que "PAL3's index1 is already
   yellow throughout gameplay". Pero el texto pinta con el 15, y PAL3[15]
   durante la partida es lo que trae `planetSmall`, no el amarillo.

3. **Las puertas selladas tampoco salian amarillas** (arreglado en la rama
   `bomberman`, no en `backto16`). El arte de celda de 16x16 pinta con
   `PAL_CUERPO` (3) y `PAL_SOMBRA` (4); el prestamo de PAL3 solo cubria 0 y 1.
   Para que el amarillo salga hay que darle esa pareja, no el indice 1.

El patron comun de los tres: **el indice que importa es el que usa el ARTE**, y
el arte de este juego usa 3/4 (celdas de muro), 11..14 (plantas), 2..13
(planetas) y 15 (texto). El 1 es el unico que casi nadie pinta.

## Reglas para no romperlo

- Toda entrada prestada se guarda con `PAL_getColor` antes de escribirla y se
  devuelve al salir, con un flag para no guardarse encima de si misma
  (`planetInkSaved`, `barInkSaved`). Si añades un prestamo, sigue ese molde.
- Antes de recolorear algo, mira con que indice esta dibujado el arte
  (`tools/*.py` lo imprime al generar).
- El usuario es daltonico: **el color nunca puede ser el unico portador de un
  estado**. Puerta sellada, bloque rompible o planeta cerrado se distinguen por
  trama, forma o valor, y el color va encima como refuerzo.
- Los niveles del VDP son 8 por canal: escribe en `RGB3_3_3_TO_VDPCOLOR` o en
  valores que ya esten en la rejilla, o veras otro color del que pusiste.
