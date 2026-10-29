# La hoja del laberinto: celdas de 16x16, una fila; las tramas las genera
# tools/maze_tiles16.py desde el arte de sprite/maze_tiles_dither.png. ROW y
# sin dedup para que el orden de las teselas sea el que supone Maze_draw.
# (La de 8x8 de la epoca de la celda pequena, sprite/maze_tiles_auto.png y
# tools/maze_tiles.py, sigue en el arbol sin usarse.)
TILESET mazeTiles "sprite/maze_tiles16.png" NONE NONE ROW
TILESET mapTiles "sprite/map_tiles.png" NONE NONE ROW
# La fuente del juego: 128x48 px = 16 columnas x 6 filas de caracteres de
# 8x8, en orden ASCII desde el espacio (0x20). Dos colores: indice 0 el
# fondo, indice 1 la tinta. Sustituye a la de SGDK en VRAM, asi que no
# ocupa ni un tile extra. NONE (sin comprimir) a proposito: una tileset
# comprimida se descomprime en el heap al cargarla, y de heap vamos justos.
TILESET gameFont "sprite/font.png" NONE NONE ROW

# Segunda fuente, solo para el HUD de juego (peticion del usuario). SGDK
# carga UNA fuente en TILE_FONT_INDEX y todos los VDP_drawText tiran de
# ella, asi que tener otra distinta obliga a un tileset propio y a dibujar
# los tiles a mano: eso es hud.c.
TILESET hudFont "sprite/font_hud.png" NONE NONE ROW

# 5 frames de 8x8 en una fila: 0 la nave normal, y 1..4 la nave estrujada
# contra el muro que acaba de golpear, en el orden DIR_UP/LEFT/DOWN/RIGHT
# de player.h (asi el frame es 1+dir, sin tabla de por medio).
# Sin comprimir: son 160 bytes de tiles y comprimida habria que
# descomprimirla en el heap, que es lo que va justo en esta consola.
SPRITE playerShip "sprite/player.png" 1 1 NONE
# 12 fotogramas de 8x8 en una fila (tools/enemy_tiles.py): por cada especie
# de enemy.h el par peligroso/vulnerable, en ese orden. Sin comprimir a
# proposito, como la nave: una tileset comprimida se descomprime en el heap
# al cargarla y de heap vamos justos.
SPRITE enemyShip "sprite/enemy.png" 1 1 NONE

# La nave gigante: el arte de 32x32 (tools/player_tiles.py) que main.c pone en
# cuanto el jugador coge la C, el primer power-up. La caja de colision sigue
# siendo la celda de 16 px, asi que el dibujo sobresale 8 px por lado.
# 6 fotogramas: los 5 de arriba y el 5 del idle, con el cuerpo hundido.
SPRITE playerShip32 "sprite/player32.png" 4 4 NONE
# Las de 16x16, que son las de siempre: la celda mide 16 y ocupan una justa.
# Las de 8x8 de aqui arriba se quedan sin usar, por si hubiera que volver.
# Los seis los arma tools/player_tiles16.py desde sprite/player16_src.png, que
# es el arte pelado y no se toca: el 5 lo calcula y a todos les pone la cara.
SPRITE playerShip16 "sprite/player16.png" 2 2 NONE
SPRITE enemyShip16 "sprite/enemy16.png" 2 2 NONE
# Los iconos de los objetos, 16x16 (tools/item_icons.py). Van con las tintas
# de las plantas, asi que el sprite se pinta con PAL0 y no gasta linea.
SPRITE itemIcons "sprite/item_icons.png" 2 2 NONE
SPRITE mapShip "sprite/map_ship.png" 1 1 BEST
SPRITE planetSmall "sprite/planet_small.png" 1 1 BEST
SPRITE planetMedium "sprite/planet_medium.png" 2 2 BEST
SPRITE planetLarge "sprite/planet_large.png" 3 3 BEST
SPRITE cursorArrow "sprite/cursor_arrow.png" 1 1 BEST
SPRITE menuSun "sprite/menu_sun.png" 4 4 BEST

WAV kick0 "sound/kick0.wav" PCM4
WAV kick1 "sound/kick1.wav" PCM4
WAV kick2 "sound/kick2.wav" PCM4
WAV kick3 "sound/kick3.wav" PCM4

WAV hihat0 "sound/hihat0.wav" PCM4
WAV hihat1 "sound/hihat1.wav" PCM4
WAV hihat2 "sound/hihat2.wav" PCM4
WAV hihat3 "sound/hihat3.wav" PCM4

# Placeholders, to be replaced with real art (user request: "que sea
# placeholder, ya lo pondre yo a mi gusto").
#
# El splash de matar enemigo NO esta aqui: sale del PSG (canal de ruido mas el
# tono 0), no de una muestra -- peticion del usuario, "me gustaria que todo
# fuese desde el chip". Lo monta sfx.c. Las muestras que hubo siguen en
# res/sound/enemykill*.wav con su tools/enemykill_splash.py por si hay que
# volver: eran seis variantes de pitch y 55 KB de ROM.
WAV letterPickup "sound/letter0.wav" PCM4

# Clic del typewriter (peticion del usuario: "un sonido muy muy discreto").
# 9 ms, el sample mas corto del juego con diferencia: a un tick cada pocos
# glifos tiene que haber terminado antes del siguiente o se solapan en un
# siseo continuo.
WAV typeTick "sound/typetick0.wav" PCM4

# La musica: una cancion por planeta, siete para ocho planetas (el ultimo repite
# la primera). Todas cierran en bucle, ver tools/xgm_loop.py. Necesitan xgmtool.
XGM musPattern  "music/pattern.xgm"
XGM musAventura "music/aventura.xgm"
XGM musHouse1   "music/house1.xgm"
XGM musHouse2   "music/house2.xgm"
XGM musHouse3   "music/house3.xgm"
XGM musHouse4   "music/house4.xgm"
XGM musHouse5   "music/house5.xgm"

# Las mismas muestras para el driver XGM, que es el que suena con la musica
# puesta: 14 kHz en vez de 16 y sin volumen por canal, asi que el nivel del
# bombo (11 de 15, su suelo en la partida) va cocido en el fichero.
WAV xgmKick0 "sound/kick_xgm0.wav" XGM
WAV xgmKick1 "sound/kick_xgm1.wav" XGM
WAV xgmKick2 "sound/kick_xgm2.wav" XGM
WAV xgmKick3 "sound/kick_xgm3.wav" XGM
WAV xgmHihat0 "sound/hihat0.wav" XGM
WAV xgmHihat1 "sound/hihat1.wav" XGM
WAV xgmHihat2 "sound/hihat2.wav" XGM
WAV xgmHihat3 "sound/hihat3.wav" XGM
WAV xgmLetterPickup "sound/letter0.wav" XGM
# Para que el driver XGM no haya que descargarlo al salir de la partida (y con
# el se pierda por donde iba la cancion), todo lo que suena FUERA de la partida
# tiene que poder salir tambien por XGM: el tic del typewriter y la percusion
# del jazz del game over, estas dos con su nivel ya cocido (tools/kick_sharpen.py).
WAV xgmTypeTick "sound/typetick0.wav" XGM
# El jazz lleva el bombo LARGO (el afilado es el del muro) y rota platos, igual
# que cuando sonaba por PCM4. Las dos copias, para los dos drivers.
WAV xgmJazzKick0 "sound/kick_jazz0.wav" XGM
WAV xgmJazzKick1 "sound/kick_jazz1.wav" XGM
WAV xgmJazzRide0 "sound/hihat_jazz0.wav" XGM
WAV xgmJazzRide1 "sound/hihat_jazz1.wav" XGM
WAV xgmJazzRide2 "sound/hihat_jazz2.wav" XGM
WAV xgmJazzRide3 "sound/hihat_jazz3.wav" XGM
WAV jazzKick0 "sound/kick_jazz0.wav" PCM4
WAV jazzKick1 "sound/kick_jazz1.wav" PCM4

# LOS NIVELES HORNEADOS (res/levels.bin, lo escribe tools/levelgen desde los
# mismos src/maze.c y src/guidemap.c que lleva la consola -- ver inc/levels.h).
# Sin comprimir y sin 'far': se lee del cartucho tal cual, celda a celda, asi
# que descomprimirlo querria un buffer en RAM, que es justo lo que esto viene a
# ahorrar.
BIN levels "levels.bin" 2 2 0 NONE FALSE
