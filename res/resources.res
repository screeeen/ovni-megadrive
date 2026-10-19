TILESET mazeTiles "sprite/maze_tiles_auto.png" NONE NONE ROW
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
SPRITE enemyShip "sprite/enemy.png" 1 1 BEST
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
# placeholder, ya lo pondre yo a mi gusto") -- one sample each, no
# variant rotation like the kick/hihat have.
WAV enemyKill "sound/enemykill0.wav" PCM4
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
WAV xgmEnemyKill "sound/enemykill0.wav" XGM
WAV xgmLetterPickup "sound/letter0.wav" XGM
