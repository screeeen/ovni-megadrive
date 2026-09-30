#ifndef _SFX_H_
#define _SFX_H_

#include <genesis.h>

// Wall-hit kick drum (user request: "cada vez que la nave golpee un
// muro suene un bombo como si fuese un bombo de una roland 909"),
// activable from the menu, default OFF ("por defecto apagado" -- see
// Sfx_isEnabled's own doc comment). Uses the Z80_DRIVER_PCM4 sound
// driver (4 channels, 8 bit, fixed 16 KHz mix rate) rather than XGM --
// this game has no music, so the simplest PCM driver capable of
// layering the kick over itself (rapid wall hits, e.g. bouncing in a
// corner) is enough; XGM would only matter if a music track needed to
// keep playing underneath.

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
void Sfx_playWallHit(void);

#endif
