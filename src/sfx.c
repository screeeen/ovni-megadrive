#include "sfx.h"
#include "resources.h"

static bool enabled = FALSE; // user request: "por defecto apagado"

// Rotating index instead of a draw from the shared random() stream --
// that stream is reserved for the map/room/item/enemy generators' own
// determinism (same reasoning as main.c's shakeFramesLeft/triggerShake
// comment). Cycling 0->1->2->3->0... also guarantees the same variant
// never plays twice in a row, which a real random pick wouldn't.
static u8 nextVariant = 0;

void Sfx_loadDriver(void)
{
    SND_PCM4_loadDriver(TRUE);
}

void Sfx_setEnabled(bool value)
{
    enabled = value;
}

bool Sfx_isEnabled(void)
{
    return enabled;
}

void Sfx_playWallHit(void)
{
    static const u8 *const kicks[4] = { kick0, kick1, kick2, kick3 };
    static const u32 kickLens[4] = { sizeof(kick0), sizeof(kick1), sizeof(kick2), sizeof(kick3) };

    if (!enabled)
        return;

    SND_PCM4_startPlay(kicks[nextVariant], kickLens[nextVariant], SOUND_PCM_CH_AUTO, FALSE);
    nextVariant = (nextVariant + 1) & 3;
}
