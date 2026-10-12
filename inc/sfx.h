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
// mix rate) rather than XGM -- this game has no music, so the simplest
// PCM driver capable of layering a few short one-shots over each other
// (a wall hit and a plant pickup landing the same frame, or rapid
// repeats of either) is enough; XGM would only matter if a music track
// needed to keep playing underneath.

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

// Enemy kill (running over a vulnerable one) and letter pickup. Both are
// PLACEHOLDERS (user request: "pon efecto de sonido cuando matas un
// enemigo y cuando coges una letra. que sea placeholder, ya lo pondre yo
// a mi gusto") -- one single sample each, synthesized rather than
// recorded, with none of the 4-variant rotation the kick and hi-hat use
// to avoid sounding mechanical. Swapping either one is just dropping a
// different mono 16-bit 16kHz wav over res/sound/enemykill0.wav /
// res/sound/letter0.wav; nothing here needs to change.
//
// Both no-op while Sfx_isEnabled() is FALSE, and both let the PCM4 driver
// auto-pick a free channel, same as Sfx_playPlantPickup (neither has a
// per-hit volume, so neither needs to know which channel it landed on).
// Call on the event's own edge, not every frame of it.
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

// Ambiente de fondo. Hay DOS modalidades y las dos estan vivas: el MENU suena
// a sala de maquinas y la PARTIDA suena melodica. Se cambia con Sfx_ambScene.
// El motor (bus, rampas de volumen, deslizamiento de tono, deriva, crescendo)
// es comun a las dos; de ahi para arriba no comparten nada.
//
//  MAQUINAS (el menu) -- sala de maquinas, por una referencia concreta del
//    usuario: "International Electromatics Headquarters Exterior", de Brian
//    Hodgson para The Invasion (Doctor Who, 1968). Sin melodia y sin acordes.
//    Cinco canales FM: dos A#1 separadas 18 centesimas que baten a 0,6 Hz (el
//    zumbido), un E3 flojo a tritono (la disonancia), un golpe que cae de tono
//    y un clanc metalico. Mas el ruido del PSG enganchado al divisor del canal
//    de tono 2, que es lo que permite BARRERLO: ese es el siseo que gira.
//    Cada capa va en un bucle de longitud prima -- 47, 157, 541 y 379 frames --
//    asi que la combinacion no se repite. En el menu entra con el zumbido (dos
//    voces, que sin las dos no hay batido) y suma una capa por planeta que se
//    selecciona: disonancia, golpe y clanc.
//
//  MELODICO (la partida) -- el chordslop: pad de dos voces con una progresion
//    de ocho acordes en Do menor que cambia cada ocho segundos y una melodia
//    encima con su propio ritmo. Tres canales FM, A#1 (58 Hz) a F3 (175 Hz).
//    Las tres voces suenan desde la primera habitacion: un acorde a medias no
//    es un acorde. Lo que crece por cruce es el VOLUMEN, no el numero de voces.
//
// El porque de cada decision esta en la cabecera de la seccion en sfx.c, y ahi
// esta tambien lo que hay que saber antes de tocar esto: el contrato con el
// driver PCM4, que escribe sus muestras como datos sin direccion sobre el
// mismo puerto, y que obliga a tomar el bus del Z80 y a devolver el latch de
// la parte 0 a $2A en cada rafaga de escrituras.
//
// Elige la escena, y con ella la modalidad: TRUE el menu (sala de maquinas),
// FALSE la partida (melodico). Calla lo que estuviera sonando, rehace timbre y
// tonos, y deja el ambiente para volver a entrar por capas. Pedir la que ya
// esta puesta NO hace nada, asi que se puede llamar sin mirar.
void Sfx_ambScene(bool menu);

// Una capa mas y un paso de crescendo, en la modalidad que este puesta. En la
// partida se llama UNA vez por cruce de habitacion (loadRoom(), por donde
// pasan todos) y ahi solo mueve el crescendo, porque la melodica ya entra con
// sus tres voces; en el menu, una por planeta que se selecciona, y ahi SI mete
// capa (peticion del usuario: "las capas se superponen cuando se seleccionan
// los planetas").
// NO cambia el acorde ni dispara nada: de eso se ocupa el reloj de
// Sfx_update. No-op si Sfx_isEnabled() es FALSE.
void Sfx_slop(void);

// Desvanece las voces y rearma el crescendo. Hay que llamarlo al SALIR de
// la partida (resetToMenu y gameOverBegin lo hacen): la envolvente FM esta
// puesta para sostener indefinidamente, asi que sin esto el acorde seguiria
// sonando en el menu. Sfx_setEnabled(FALSE) tambien lo llama.
void Sfx_slopStop(void);

// Quien manda en el canal de RUIDO del PSG. El visor de mapa lo usa para su
// estatica y la sala de maquinas para su siseo, asi que el visor lo pide al
// abrirse (FALSE) y lo devuelve al cerrarse (TRUE). Con la sala en el menu los
// dos ya no coinciden nunca -- el visor es pantalla de partida y la partida
// suena melodica, que no toca el PSG -- asi que hoy no arbitra nada. Se queda
// porque no cuesta y vuelve a hacer falta si las escenas se intercambian.
void Sfx_ambNoise(bool own);

// Una vez por frame desde el bucle principal, al lado de updateShake(). Esto
// SI hace trabajo de verdad: lleva el pulso (medio segundo, 16 por compas) del
// que cuelgan el cambio de acorde y las notas de la melodia, y recalcula el
// tono y el volumen de cada voz (la envolvente del chip esta puesta para
// sostener; el volumen lo lleva este codigo escribiendo TL). Escribe solo lo
// que ha cambiado, y cuando hay algo que escribir toma el bus del Z80 una vez
// por llamada. Es un no-op mientras no haya ninguna voz viva.
void Sfx_update(void);

#endif
