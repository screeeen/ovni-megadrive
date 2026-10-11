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

// Chordslop. Un acorde de cinco sinusoidales (FM, canales 1 a 5 del YM2612)
// que suena siempre y no cambia de identidad: lo que varia son gestos de
// pitch bend que salen de la nota y vuelven a ella, con los destinos
// restringidos a notas del acorde. El modelo es el Chord Slop Lab que paso el
// usuario como referencia; el porque de cada decision esta en la cabecera de
// la seccion en sfx.c.
//
// Sfx_slop() se llama UNA vez por cruce de habitacion -- loadRoom() en
// main.c, que es por donde pasan todos. Hace dos cosas: arranca una voz mas
// (las cinco entran de una en una, una por habitacion) y reparte gestos
// nuevos a las voces que esten quietas. No-op si Sfx_isEnabled() es FALSE.
void Sfx_slop(void);

// Calla las cinco voces y rearma el crescendo. Hay que llamarlo al SALIR de
// la partida (resetToMenu y gameOverBegin lo hacen): la envolvente FM esta
// puesta para sostener indefinidamente, asi que sin esto el acorde seguiria
// sonando en el menu. Sfx_setEnabled(FALSE) tambien lo llama.
void Sfx_slopStop(void);

// Una vez por frame desde el bucle principal, al lado de updateShake(). Esto
// SI hace trabajo de verdad: el YM2612 no tiene bends ni vibrato, asi que el
// tono de cada voz se recalcula y se reescribe cada frame. Toma el bus del
// Z80 una vez por llamada; es un no-op mientras no haya ninguna voz viva.
void Sfx_update(void);

#endif
