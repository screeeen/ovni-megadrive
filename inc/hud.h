#ifndef _HUD_H_
#define _HUD_H_

#include <genesis.h>

#define HUD_FONT_TILE_COUNT 96   // font_hud.png: 128x48

// Fuente propia para el HUD de juego (peticion del usuario: una fuente
// distinta de la del resto). SGDK carga UNA sola fuente en
// TILE_FONT_INDEX y todos los VDP_drawText tiran de ella, asi que tener
// otra obliga a su propio tileset y a escribir los tiles a mano: eso es
// lo que hacen estas dos funciones.
void Hud_loadFont(void);                        // una vez, al arrancar
void Hud_drawText(const char *s, u16 x, u16 y); // BG_B, prioridad alta

#endif
