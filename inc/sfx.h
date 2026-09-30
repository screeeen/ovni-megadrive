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

// TRUE/FALSE -- starts FALSE (user request: "por defecto apagado").
// Toggled from the menu (main.c); persists across games/resets for the
// rest of this power-on session, same lifetime as controlMode.
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

#endif
