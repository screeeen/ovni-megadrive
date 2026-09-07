#include <genesis.h>
#include "resources.h"
#include "maze.h"
#include "player.h"

static Player player;
static Sprite *playerSprite;

static void newGame(void)
{
    Maze_generate();
    Maze_draw();
    Player_init(&player);
    SPR_setPosition(playerSprite, player.x, player.y);
}

int main(bool hardReset)
{
    u16 prevState = 0;

    VDP_setPlaneSize(64, 32, TRUE);
    SPR_init();

    Maze_loadGraphics();

    PAL_setPalette(PAL1, playerShip.palette->data, DMA);
    playerSprite = SPR_addSprite(&playerShip, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));

    JOY_init();

    newGame();

    while (TRUE)
    {
        const u16 state = JOY_readJoypad(JOY_1);

        // Edge-triggered: rotate once per press. A matches the keydown/SPACE
        // handler in the original js13k game (counter-clockwise); B is the
        // new opposite turn (clockwise).
        if ((state & BUTTON_A) && !(prevState & BUTTON_A))
            Player_rotateCCW(&player);
        if ((state & BUTTON_B) && !(prevState & BUTTON_B))
            Player_rotateCW(&player);

        prevState = state;

        if (Player_update(&player))
            newGame();

        SPR_setPosition(playerSprite, player.x, player.y);
        SPR_update();

        SYS_doVBlankProcess();
    }

    return 0;
}
