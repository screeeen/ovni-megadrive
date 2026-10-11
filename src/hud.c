#include "hud.h"
#include "resources.h"
#include "maze.h"
#include "guidemap.h"
#include "menu.h"

// Detras de todo lo demas que reserva tiles: maze, mapa guia, el scratch
// del ruido y el menu. Mismo encadenado que ya usaban entre si.
#define HUD_FONT_BASE (TILE_USER_INDEX + MAZE_TILE_COUNT + GUIDEMAP_TILE_COUNT \
                       + MAZE_SCRATCH_TILE_COUNT + MENU_TILE_COUNT)

void Hud_loadFont(void)
{
    VDP_loadTileSet(&hudFont, HUD_FONT_BASE, DMA);
}

void Hud_drawText(const char *s, u16 x, u16 y)
{
    u16 i = 0;

    // Prioridad 1 en el propio atributo del tile, que es lo que
    // VDP_setTextPriority(1) hacia por nosotros: el HUD va sobre el
    // laberinto, que es de prioridad baja.
    while (s[i])
    {
        const u8 c = (u8) s[i];
        const u16 g = ((c >= 32) && (c < 128)) ? (u16) (c - 32) : 0;

        VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 1, FALSE, FALSE, (u16) (HUD_FONT_BASE + g)),
                         (u16) (x + i), y);
        i++;
    }
}
