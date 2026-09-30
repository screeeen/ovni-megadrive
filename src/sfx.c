#include "sfx.h"
#include "resources.h"

static bool enabled = FALSE; // user request: "por defecto apagado"

// Rotating index instead of a draw from the shared random() stream --
// that stream is reserved for the map/room/item/enemy generators' own
// determinism (same reasoning as main.c's shakeFramesLeft/triggerShake
// comment). Cycling 0->1->2->3->0... also guarantees the same variant
// never plays twice in a row, which a real random pick wouldn't. Doubles
// as the PCM4 channel index (see Sfx_playWallHit): SND_PCM4_startPlay's
// SOUND_PCM_CH_AUTO auto-selection gives no way to find out afterwards
// which channel it actually picked, and setting a per-hit volume (below)
// needs a known channel to call SND_PCM4_setVolume on -- 4 variants and 4
// channels line up exactly, so rotating both off the same counter still
// spreads rapid repeats (corner-bouncing) across different channels the
// same way SOUND_PCM_CH_AUTO's free-channel scan would have.
static u8 nextVariant = 0;

// Volume scaling (user request: "como el screenshake, el volumen que sea
// proporcional a la distancia") -- same shape as main.c's own
// triggerShake: a floor level (so even the shortest bump is audible) plus
// +1 per SFX_VOL_DIST_PER_LEVEL px of run-up, capped at the driver's max.
// Unlike the shake (capped at 3 of its own possible levels to keep the
// screen offset subtle), volume uses the PCM4 driver's full 0-15 range --
// there's no equivalent reason to hold this one back.
#define SFX_VOL_MIN            6
#define SFX_VOL_MAX            15
#define SFX_VOL_DIST_PER_LEVEL 16

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

void Sfx_playWallHit(u16 distance)
{
    static const u8 *const kicks[4] = { kick0, kick1, kick2, kick3 };
    static const u32 kickLens[4] = { sizeof(kick0), sizeof(kick1), sizeof(kick2), sizeof(kick3) };
    const SoundPCMChannel channel = (SoundPCMChannel) nextVariant;
    u16 vol = SFX_VOL_MIN + (distance / SFX_VOL_DIST_PER_LEVEL);

    if (!enabled)
        return;

    if (vol > SFX_VOL_MAX)
        vol = SFX_VOL_MAX;

    // Set before starting playback so the very first mixed sample of this
    // hit already uses the new level, not whatever this channel was left
    // at by its previous hit.
    SND_PCM4_setVolume(channel, (u8) vol);
    SND_PCM4_startPlay(kicks[nextVariant], kickLens[nextVariant], channel, FALSE);
    nextVariant = (nextVariant + 1) & 3;
}
