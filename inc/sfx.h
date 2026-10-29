#ifndef _SFX_H_
#define _SFX_H_

#include <genesis.h>

// Wall-hit kick drum (user request: "cada vez que la nave golpee un
// muro suene un bombo como si fuese un bombo de una roland 909") and
// plant-pickup closed hi-hat (user request: "implementa un hi hat
// cerrado que suene como el de la 909... cuando colisiona con las
// plantas"), both activable from the menu as one single toggle, default
// OFF ("por defecto apagado" -- see Sfx_isEnabled's own doc comment).
// Uses the Z80_DRIVER_PCM4 sound driver (4 channels, 8 bit, fixed 16 KHz
// mix rate) fuera de la partida, y el XGM dentro, que es donde suena la
// musica: los dos no conviven, asi que se cambia de driver en cada cambio
// de escena. Lo que cuesta el cambio esta en la seccion de la musica en
// sfx.c, y lo que cuesta de verdad es el volumen por canal, que el XGM no
// tiene: con la musica puesta el nivel va cocido en el wav (los
// res/sound/kick_xgm*.wav) y el bombo ya no suena mas fuerte cuanto mas lejos.

// Loads the Z80_DRIVER_PCM4 driver. Call once at boot, same place as
// Menu_loadGraphics()/Maze_loadGraphics().
void Sfx_loadDriver(void);

// TRUE/FALSE -- starts TRUE (user request: "sonido es ON por defecto";
// it used to start off). Still toggled with C from the menu (main.c),
// just no longer announced there -- see drawMenu. Persists across
// games/resets for the rest of this power-on session.
void Sfx_setEnabled(bool enabled);
bool Sfx_isEnabled(void);

// Plays one wall-hit kick, picking one of 4 pre-baked near-identical
// variants (res/resources.res's kick0..kick3 -- see docs/spec-mapa-guia.md
// §47 for how they were synthesized) so it's never the exact same sample
// twice in a row, without needing real-time pitch control (the PCM4
// driver has none). No-op while Sfx_isEnabled() is FALSE. Caller is
// responsible for calling this only on the actual wall-hit edge (same
// edge-trigger main.c's own triggerShake() already uses), not every
// frame the ship stays blocked.
// distance (px the ship had slid before this impact) sets the volume,
// same "amplitude proportional to travel" idea as main.c's own
// triggerShake(distance) -- user request: "como el screenshake, el
// volumen que sea proporcional a la distancia". Pass the exact same
// value given to triggerShake() at each call site.
void Sfx_playWallHit(u16 distance);

// Plays one plant-pickup hi-hat, picking one of 4 pre-baked near-
// identical closed-hihat variants (res/resources.res's hihat0..hihat3 --
// see docs/spec-mapa-guia.md's plant-hihat section for how they were
// synthesized) so it's never the exact same sample twice in a row, same
// "bake the variation into a few samples, rotate through them" approach
// as Sfx_playWallHit. No-op while Sfx_isEnabled() is FALSE. Unlike
// Sfx_playWallHit, this has no distance/volume input (no "louder for a
// harder hit" concept for a pickup) and lets the PCM4 driver auto-pick a
// free channel instead of a fixed rotating one -- it doesn't need to call
// SND_PCM4_setVolume, so it doesn't need to know which channel it landed
// on the way Sfx_playWallHit does.
void Sfx_playPlantPickup(void);

// Matar un enemigo y coger una letra. Los dos nacieron PLACEHOLDER (peticion
// del usuario: "pon efecto de sonido cuando matas un enemigo y cuando coges
// una letra. que sea placeholder, ya lo pondre yo a mi gusto").
//
// EL DE LA MUERTE SALE DEL CHIP, no de una muestra (peticion del usuario: "me
// gustaria que todo fuese desde el chip"): el canal de ruido del PSG con el
// tono 0 por debajo haciendo de golpe, y el pitch sorteado en cada muerte --
// ver el bloque del splash en sfx.c. Por eso no tiene ni rama de musicOn ni
// canal del PCM4: el PSG suena igual con la cancion puesta y sin ella.
//
// EL DE LA LETRA sigue siendo una muestra, una sola y sintetizada, sin la
// rotacion de 4 variantes que llevan el bombo y el hi-hat para no sonar
// mecanicos. Cambiarlo es dejar otro wav mono de 16 bits y 16 kHz encima de
// res/sound/letter0.wav; aqui no hay que tocar nada.
//
// Los dos se callan con Sfx_isEnabled() en FALSE. El de la letra deja que el
// driver PCM4 le busque canal libre, como Sfx_playPlantPickup (no tiene
// volumen por golpe, asi que no necesita saber donde cayo).
// Llamarlos en el flanco del suceso, no en cada frame.
// Typewriter click, for the text that reveals itself a glyph at a time
// (main.c's typeStep). Deliberately the quietest thing in the game --
// volume 2 of 15, where the wall-hit kick runs 6..15 (user request: "un
// sonido muy muy discreto"). No-op while Sfx_isEnabled() is FALSE.
//
// The CALLER decides how often: at 3 glyphs a frame a tick per glyph
// would be 180 a second, which is a hiss and not a typewriter. main.c
// fires one every TYPE_TICK_EVERY glyphs instead.
void Sfx_playTypeTick(void);

void Sfx_playEnemyKill(void);
void Sfx_playLetterPickup(void);

// LA MUSICA. La partida suena a musica: una cancion por planeta (res/music,
// siete para ocho, el ultimo repite la primera) por el driver XGM del Z80, que
// se lleva el chip entero mientras dura. El menu va sin fondo: solo efectos.
//
// El XGM no convive con el PCM4, que es quien dispara las muestras fuera de la
// partida, asi que cada cambio de escena reinstala el driver que toca -- ver
// musicStart/musicStop en sfx.c. Ese cambio RESETEA los dos chips de sonido,
// y por eso importa el orden en todo lo que escriba el YM alrededor.
//
// Aqui hubo dos motores de ambiente procedural, uno de sala de maquinas para
// el menu y un pad de acordes con melodia para la partida. Se fueron enteros
// (peticion del usuario): con la musica puesta el de la partida ya no llegaba
// a sonar nunca.

// Elige la escena: TRUE el menu (calla la cancion), FALSE la partida (la
// arranca desde el principio; volver a entrar la relanza).
//
// Suena la cancion del planeta que Sfx_setPlanet dejo apuntado, asi que el
// orden importa: primero se elige el planeta y luego se cambia de escena.
void Sfx_musicScene(bool menu);

// Apunta de que planeta es la cancion. El indice es el del preset,
// 0..MENU_PLANET_COUNT-1, y va por numero de planeta y no por fila sorteada:
// los nombres y los efectos del menu se rebarajan en cada vuelta, pero el
// planeta 3 suena siempre igual.
//
// main.c lo llama en cada movimiento del cursor del menu, asi que cuando
// Sfx_musicScene(FALSE) arranca la musica el planeta ya es el bueno y no hace
// falta pasarselo. Se apunta SIEMPRE, aunque el sonido este apagado ahora:
// la partida lo lee al cambiar de escena y puede encenderse despues.
void Sfx_setPlanet(u8 planet);

// Calla la cancion y corta en seco el jazz del game over. Hay que llamarlo al
// SALIR de la partida -- resetToMenu y gameOverBegin lo hacen -- y lo llama
// tambien Sfx_setEnabled(FALSE).
// Cambia la velocidad de la musica: 100 es la suya, menos la ralentiza. La usa
// la pantalla de muerte para arrastrar la cancion mientras se va a negro.
// Acuerdate de devolverla a 100.
void Sfx_musicTempoPercent(u16 porcentaje);

void Sfx_musicStop(void);

// Igual que la de arriba pero dejando la cancion donde esta: al volver a la
// misma fase sigue por donde iba en vez de empezar de cero (peticion del
// usuario). Es la que hay que usar al salir de una partida que se puede
// retomar -- al menu o al game over.
void Sfx_musicPause(void);

// Una vez por frame desde el bucle principal, al lado de updateShake(). Hace
// poco: baja la cuenta atras con la que el visor sabe que muestra sigue
// sonando, y lleva el jazz del game over mientras dura. La cancion la lleva el
// Z80 por su cuenta y no pide nada por frame.
void Sfx_update(void);


// --- EL INVENTARIO DE SONIDOS ----------------------------------------------
//
// Todos los sonidos que el juego puede emitir, cada uno con su CODENAME
// (peticion del usuario: "quiero que identifiques cada sonido en pantalla,
// escribe su codename para que podamos hablar de ello"). De aqui en adelante
// se les llama asi, en el codigo y al hablar. La tabla de nombres esta en
// sfx.c y el visor la lee de ahi, asi que no pueden separarse.
//
//   0 KICK    bombo 909 al chocar con un muro      4 variantes que rotan
//   1 HAT     hi-hat cerrado al coger una planta   4 variantes
//   2 KILL    matar un enemigo                     PLACEHOLDER
//   3 LETTER  coger una letra                      PLACEHOLDER
//   4 TYPE    clic del typewriter                  lo mas flojo del juego
//
//   5 JAZZ    la cadencia del game over            sus cuatro voces como una
//   6 BRUSH   el cepillo del jazz                  el ruido a contratiempo
//
//   7 MUSIC   la cancion del planeta               res/music, driver XGM
//
// Los 0..4 son MUESTRAS y los demas son FM o PSG. Los cinco primeros son los
// que se disparan, de ahi SFX_ID_SHOTS.

#define SFX_ID_KICK   0
#define SFX_ID_HAT    1
#define SFX_ID_KILL   2
#define SFX_ID_LETTER 3
#define SFX_ID_TYPE   4
#define SFX_ID_JAZZ   5
#define SFX_ID_BRUSH  6
#define SFX_ID_MUSIC  7
#define SFX_ID_COUNT  8
#define SFX_ID_SHOTS  5    // los que se disparan: 0..4
#define SFX_ID_NONE  0xFF

// El nombre y la linea de "que es", para el visor. Nunca NULL.
const char *Sfx_debugName(u8 id);
const char *Sfx_debugWhat(u8 id);

// Cuanto suena AHORA mismo, de 0 (nada) a 15.
//
// Para las VOCES FM y el siseo es el nivel de verdad, leido de lo que se le ha
// escrito al chip. Para las MUESTRAS no: el driver PCM4 no sabe decir si una
// muestra sigue, asi que es una cuenta atras armada al dispararlas, con un
// suelo de 0,2 s para que TYPE (18 ms, un solo frame) llegue a verse. O sea
// que ahi la barra dice "acaba de sonar", no "esta sonando".
u8 Sfx_debugLevel(u8 id);

// Si ese sonido puede sonar en la escena que hay puesta. Una voz FM de la
// partida no existe en el menu y al reves, y la partida tiene 3 o 4 segun si
// el planeta lleva firma.
bool Sfx_debugExists(u8 id);

// Deja sonando SOLO ese, callando el resto del ambiente, que es lo que hace
// falta para decidir cual sobra. SFX_ID_NONE lo quita y todo vuelve.
void Sfx_debugSolo(u8 id);

// QUE FILA del reparto esta sonando, de 0 a 7, o 0xFF si no hay ninguna (la
// sala de maquinas entera). NO es el numero de planeta: el reparto
// planeta->sonido es una permutacion que se rehace en cada arranque, asi que
// el planeta 3 no suena igual de una partida a otra -- de ahi el "a veces".
// La fila es lo unico que identifica un sonido de forma estable, y es el
// numero con el que hay que nombrarlos al pedir un cambio.


// La nota de reposo de esa fila, en decimas de Hz. 0 si no hay fila.


// --- EL JAZZ DEL GAME OVER -------------------------------------------------
//
// Peticion del usuario: "quiero que suene jazz en game over", luego "lo quiero
// mas largo, con algo de percusion" y luego "con una octava, que sea mas
// upbeat". Trece acordes en Do menor en tres frases, CINCO voces FM (bajo
// andando, tres de acorde conducidas nota a nota y la melodia doblada una
// octava arriba) y unos ONCE segundos: 11,2 a 60 Hz, de los cuales los 4
// ultimos son la cola del acorde final.
//
// Y percusion, con lo que ya habia: el hi-hat hace de plato con el patron
// spang-a-lang (un golpe por pulso mas una corchea swing a dos tercios), el
// bombo marca el cambio de acorde y nada mas, y el cepillo del contratiempo
// sale del canal de ruido del PSG. Todo cuelga del mismo reloj de pulso que
// el bajo, asi que va a tiempo con el. El canal 4 del PCM4 se deja libre a
// proposito: en esta pantalla el marcador suelta un TYPE cada 4 frames.
//
// No es una modalidad del ambiente: es un corte con principio y fin que se
// muere solo, y mientras dura es lo unico que escribe el chip.
//
// Lo llama gameOverBegin EN LUGAR de Sfx_slopStop -- lo hace el por dentro.
// Se corta con Sfx_slopStop (o sea, al volver al menu, o al apagar el sonido),
// y no hace nada si el sonido esta apagado.
void Sfx_jazzGameOver(void);

#endif
