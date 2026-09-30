#include "sfx.h"
#include "resources.h"

static bool enabled = FALSE; // user request: "por defecto apagado"

// Rotating indices instead of a draw from the shared random() stream --
// that stream is reserved for the map/room/item/enemy generators' own
// determinism (same reasoning as main.c's shakeFramesLeft/triggerShake
// comment). Cycling 0->1->2->3->0... also guarantees the same variant
// never plays twice in a row, which a real random pick wouldn't.
//
// nextKickVariant doubles as the PCM4 channel index (see
// Sfx_playWallHit): SND_PCM4_startPlay's SOUND_PCM_CH_AUTO auto-selection
// gives no way to find out afterwards which channel it actually picked,
// and setting a per-hit volume needs a known channel to call
// SND_PCM4_setVolume on -- 4 variants and 4 channels line up exactly, so
// rotating both off the same counter still spreads rapid repeats
// (corner-bouncing) across different channels the same way
// SOUND_PCM_CH_AUTO's free-channel scan would have.
static u8 nextKickVariant = 0;
// nextHihatVariant only picks the sample -- Sfx_playPlantPickup has no
// per-hit volume, so (unlike the kick) it has no reason to know its own
// channel and just lets SOUND_PCM_CH_AUTO pick whichever of the 4 PCM4
// channels is free, same as the kick used to before spec §47's volume
// scaling forced it off of that.
static u8 nextHihatVariant = 0;

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
    const SoundPCMChannel channel = (SoundPCMChannel) nextKickVariant;
    u16 vol = SFX_VOL_MIN + (distance / SFX_VOL_DIST_PER_LEVEL);

    if (!enabled)
        return;

    if (vol > SFX_VOL_MAX)
        vol = SFX_VOL_MAX;

    // Set before starting playback so the very first mixed sample of this
    // hit already uses the new level, not whatever this channel was left
    // at by its previous hit.
    SND_PCM4_setVolume(channel, (u8) vol);
    SND_PCM4_startPlay(kicks[nextKickVariant], kickLens[nextKickVariant], channel, FALSE);
    nextKickVariant = (nextKickVariant + 1) & 3;
}

void Sfx_playPlantPickup(void)
{
    static const u8 *const hihats[4] = { hihat0, hihat1, hihat2, hihat3 };
    static const u32 hihatLens[4] = { sizeof(hihat0), sizeof(hihat1), sizeof(hihat2), sizeof(hihat3) };

    if (!enabled)
        return;

    SND_PCM4_startPlay(hihats[nextHihatVariant], hihatLens[nextHihatVariant], SOUND_PCM_CH_AUTO, FALSE);
    nextHihatVariant = (nextHihatVariant + 1) & 3;
}
