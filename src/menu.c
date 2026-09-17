#include "menu.h"
#include "resources.h"

// 320x224 screen. Sun sits left of dead-center vertically so the
// biggest orbit's top edge (with room for the cursor arrow above
// whichever planet sits there) stays clear of the title text, and its
// bottom edge stays clear of the size-name/hint text below (main.c's
// drawMenu()) -- main.c's drawMenu() moved its title up and its bottom
// text down (spec §32quat) to free up the extra vertical room these
// wider orbits need.
#define SUN_CENTER_X 160
#define SUN_CENTER_Y 122

// menuSun is a real 32x32 (4x4 tile) sprite, not a BG-tile fill (spec
// §32septies, user request: "por que el bloque del centro no es un
// círculo?") -- a BG_A tile fill can only test whole 8x8 tiles against
// the sun's radius, and at the size the sun needs to be that produced a
// plain blocky rectangle, not a circle (verified by hand: with the old
// SUN_RADIUS_PX=12, exactly the 2x3 block of tiles centered on the sun
// passed the per-tile radius test, no partial/corner tiles at all).
// menu_sun.png bakes a real per-pixel circular mask instead, same
// technique the planet sprites already use successfully at this scale.
// Its own raster scan order is dark-first/violet-second, matching
// EVERY other sprite in this game (playerShip, enemyShip, mapShip, the
// planets, the cursor) -- so it renders using PAL1 (playerShip/mapShip)
// with zero extra palette work needed for its own compiled pixel data.
#define SUN_HALF_PX 16
// Absolute CRAM index of PAL1's index1 -- the same slot main.c's
// PLAYER_SHIP_INK_INDEX flips between violet and white for the map-ship
// blink (spec §23). Reused here as a temporary override: playerShip and
// mapShip are both hidden for the entire time the menu is shown (main.c
// hides them at boot and in resetToMenu()), so nothing else depends on
// this slot's color while the sun sprite needs it to read white instead
// of its default violet. Menu_setVisible flips it to white on entry and
// restores playerShip's own violet on exit, mirroring the exact
// override/restore pattern main.c's blink code already uses on this
// same slot.
#define SUN_INK_INDEX ((PAL1 * 16) + 1)

// Elliptical, not circular (spec §31) -- makes better use of the
// 320x224 screen's aspect ratio than a true circle would. Radii grow
// with MENU_PLANET_COUNT so every orbit reads as a clearly nested ring,
// same order as main.c's sizePresets[] (smallest/fewest-letter map =
// innermost, spec §33). 7 rings now (spec §33, user request: 4 new
// smaller phases added ahead of the original 3) -- recomputed from
// scratch rather than just prepending to spec §32quinquies's 3 values,
// since 4 more (smaller) rings need to fit between the sun and the old
// innermost ring too:
// - Outer ring (index 6, the old largest/10x8 preset) keeps the exact
//   same hard ceiling as before: X <= screen half-width minus the
//   largest planet sprite's half-width and a small margin
//   (160-12-4=144); Y <= the gap between the title and bottom text
//   minus the cursor's own clearance above the biggest planet (62).
// - Inner ring (index 0, the new smallest/3x3/1-letter preset) has to
//   clear the SUN sprite (menuSun, ~13px visual radius) plus its own
//   planet half-width (4) plus a few px of gap, on the tighter axis
//   (Y, since the ellipse is wider than tall): 13+4+3=20 is the floor
//   used here for Y; X uses the same X/Y aspect ratio as the outer
//   ring (144/62≈2.32) so every ring reads as a consistent ellipse,
//   not just the outer one: 20*2.32≈46.
// - The other 5 rings are linearly interpolated between those two
//   endpoints on each axis independently.
// Still used to ANIMATE each planet's position (Menu_update below) even
// though the orbit lines themselves are no longer drawn (spec §32sexies,
// user request) -- the planets still travel these exact elliptical
// paths, just without a visible line traced under them.
static const s16 orbitRadiusX[MENU_PLANET_COUNT] = { 46, 62, 79, 95, 111, 128, 144 };
static const s16 orbitRadiusY[MENU_PLANET_COUNT] = { 20, 27, 34, 41, 48, 55, 62 };

// Per-planet angular speed (degrees/frame at 60fps) and starting angle
// (spread apart so the planets don't all launch aligned) -- purely
// decorative, no gameplay meaning. Inner planet orbits faster, like a
// real solar system. 7 planets now (spec §33, user request).
static const fix16 orbitSpeed[MENU_PLANET_COUNT] = {
    FIX16(1.8), FIX16(1.6), FIX16(1.4), FIX16(1.2), FIX16(1.0), FIX16(0.8), FIX16(0.5)
};
static fix16 orbitAngle[MENU_PLANET_COUNT];

// Sprite half-width/half-height in pixels, matching planetSmall (8x8),
// planetMedium (16x16), planetLarge (24x24) -- needed both to convert a
// computed CENTER position into SPR_setPosition's top-left corner, and
// to know how far above each planet's own edge the cursor should clear
// (spec §31: "justo encima del planeta"). Only 3 distinct sizes exist
// on real hardware (1/2/3 tiles/side -- 4 tiles/side is reserved for
// the sun, spec §32septies), so with 7 planets (spec §33) each size is
// now reused across a small group of adjacent (by orbit radius) tiers:
// small for the 3 tiniest/new presets, medium for the next 2, large for
// the original 2 biggest -- orbit radius is what actually communicates
// the 7-way progression, sprite size is a coarser secondary cue.
static const s16 planetHalfSize[MENU_PLANET_COUNT] = { 4, 4, 4, 8, 8, 12, 12 };
#define CURSOR_GAP_PX 4  // visible gap between the cursor arrow and the planet's own top edge
#define CURSOR_HALF_W 4  // cursorArrow is 8x8
#define CURSOR_HALF_H 4

static Sprite *planetSprites[MENU_PLANET_COUNT];
static Sprite *cursorSprite;
static Sprite *sunSprite;

void Menu_loadGraphics(void)
{
    // PAL3 is otherwise unused by the rest of the game (PAL0=maze/text,
    // PAL1=playerShip/mapShip, PAL2=enemyShip) -- dedicated here so the
    // menu's planet/cursor sprites don't disturb any in-game palette.
    PAL_setPalette(PAL3, planetSmall.palette->data, DMA);

    // Matches planetHalfSize[] above: small for the 3 new tiny presets,
    // medium for the next 2, large for the original 2 biggest (spec §33).
    planetSprites[0] = SPR_addSprite(&planetSmall,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[1] = SPR_addSprite(&planetSmall,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[2] = SPR_addSprite(&planetSmall,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[3] = SPR_addSprite(&planetMedium, 0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[4] = SPR_addSprite(&planetMedium, 0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[5] = SPR_addSprite(&planetLarge,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    planetSprites[6] = SPR_addSprite(&planetLarge,  0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    cursorSprite = SPR_addSprite(&cursorArrow, 0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));

    // Sun uses PAL1 (see SUN_INK_INDEX above) -- no PAL_setPalette call
    // here, its color comes from whatever Menu_setVisible pokes into
    // that slot, not from menuSun's own compiled palette data. Position
    // is fixed (the sun never moves), so it's set once here rather than
    // every frame in Menu_update.
    sunSprite = SPR_addSprite(&menuSun, SUN_CENTER_X - SUN_HALF_PX, SUN_CENTER_Y - SUN_HALF_PX,
                               TILE_ATTR(PAL1, TRUE, FALSE, FALSE));

    Menu_setVisible(FALSE);
}

void Menu_setVisible(bool visible)
{
    u8 i;

    for (i = 0; i < MENU_PLANET_COUNT; i++)
        SPR_setVisibility(planetSprites[i], visible ? VISIBLE : HIDDEN);
    SPR_setVisibility(cursorSprite, visible ? VISIBLE : HIDDEN);
    SPR_setVisibility(sunSprite, visible ? VISIBLE : HIDDEN);

    // Flip PAL1's index1 between white (menu showing, spec §32septies)
    // and playerShip's own violet (leaving the menu) -- safe because
    // playerShip/mapShip, the only other things using PAL1, are always
    // hidden for the entire time the menu is visible (main.c).
    if (visible)
        PAL_setColor(SUN_INK_INDEX, RGB24_TO_VDPCOLOR(0xFFFFFF));
    else
        PAL_setColor(SUN_INK_INDEX, playerShip.palette->data[1]);

    // Fresh entry into the menu always restarts the orbit animation from
    // the same spread-out starting angles -- simpler and just as good
    // visually as trying to resume mid-orbit, and avoids needing to
    // persist orbitAngle[] across a game played in between.
    if (visible)
    {
        u8 j;

        for (j = 0; j < MENU_PLANET_COUNT; j++)
            orbitAngle[j] = FIX16((360 / MENU_PLANET_COUNT) * j);
    }
}

void Menu_update(u8 selectedIndex)
{
    u8 i;
    s16 selCx = SUN_CENTER_X, selCy = SUN_CENTER_Y; // fallback, always overwritten below

    for (i = 0; i < MENU_PLANET_COUNT; i++)
    {
        fix16 fx, fy;
        s16 cx, cy;

        orbitAngle[i] += orbitSpeed[i];
        if (orbitAngle[i] >= FIX16(360))
            orbitAngle[i] -= FIX16(360);

        F16_computePositionEx(&fx, &fy, FIX16(SUN_CENTER_X), FIX16(SUN_CENTER_Y),
                               orbitAngle[i], FIX16(1), FIX16(orbitRadiusX[i]), FIX16(orbitRadiusY[i]));

        cx = F16_toRoundedInt(fx);
        cy = F16_toRoundedInt(fy);

        SPR_setPosition(planetSprites[i], cx - planetHalfSize[i], cy - planetHalfSize[i]);

        if (i == selectedIndex)
        {
            selCx = cx;
            selCy = cy;
        }
    }

    // Cursor: horizontally centered on the selected planet, its bottom
    // edge CURSOR_GAP_PX above that planet's own top edge (spec §31,
    // "justo encima del planeta") -- tracks the live position computed
    // above, not a separately-maintained angle.
    SPR_setPosition(cursorSprite,
                     selCx - CURSOR_HALF_W,
                     selCy - planetHalfSize[selectedIndex] - CURSOR_GAP_PX - (2 * CURSOR_HALF_H));
}
