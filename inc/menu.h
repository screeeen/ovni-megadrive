#ifndef _MENU_H_
#define _MENU_H_

#include <genesis.h>

// Solar-system start menu (spec §31): one planet per map-size preset
// (main.c's sizePresets[]), orbiting a sun at the screen's center on
// visible elliptical orbit lines. A downward-pointing cursor arrow
// tracks the currently selected planet, positioned right above its
// live (moving) location every frame -- LEFT/RIGHT still cycles the
// selection (main.c owns that index, same as before this feature),
// BUTTON_A confirms and starts the game.
// Must equal main.c's SIZE_PRESET_COUNT -- main.c's sizePresetIndex is
// passed straight into Menu_update() as the selected planet index. 8
// planets: the original 3, 4 smaller/fewer-letter phases added ahead of
// those (spec §33), and a 1x1 single-room tombo test planet (user
// request) added ahead of ALL of those as the new innermost orbit --
// only 4 discrete sprite sizes exist on real hardware (1-4 tiles/side),
// so menu.c reuses them across multiple planets; the orbit radius
// (closer = earlier/easier) is the primary visual progression now, not
// sprite size alone.
#define MENU_PLANET_COUNT 8

// Uploads the menu's sprite graphics/palettes to VRAM/CRAM: planets and
// cursor on PAL3 (unused by anything else in the game, spec §31's note
// on this), and the sun sprite on PAL1 -- a real circular sprite, not a
// BG-tile fill (spec §32septies), since BG tiles are too coarse (8x8) to
// read as round at this size. Call once at boot.
void Menu_loadGraphics(void);

// Stamps the big 16x16-per-letter OVNI into BG_A's top right corner (user
// request: the sound-toggle line is gone and the title took its place,
// right-aligned and larger than the 8x8 font can be). Call after clearing
// BG_A, every time the menu is redrawn -- the tiles themselves are
// uploaded once by Menu_loadGraphics.
void Menu_drawTitle(void);

// Shows/hides the planet sprites, the cursor arrow and the sun (spec
// §31, §32septies) -- call with TRUE when entering the menu, FALSE when
// leaving it (both ways: starting a game, or the reset combo bringing
// the menu back). Also flips the sun's color between white (visible)
// and playerShip's own violet (hidden) on PAL1's shared slot -- see
// SUN_INK_INDEX in menu.c -- and, the same way, PAL2's slot between
// yellow and enemyShip's own violet (spec §45, for completed planets --
// see Menu_update below).
// Hides the menu's own sprites WITHOUT touching any palette -- see
// Menu_hideSprites' own comment in menu.c for why that separation
// exists. Menu_setVisible(FALSE) still does both, for callers that
// aren't in the middle of a fade.
void Menu_hideSprites(void);

// Fades the menu's sprites up from the background, once the screen
// itself is already in (user request). Call right after the main fade,
// with the sprites still hidden -- it makes them visible itself, at the
// step where their ink is still exactly the background colour. Takes
// Menu_update's own arguments because it runs that too, every frame, so
// the orbits are already turning as the planets appear.
void Menu_fadeInSprites(u8 selectedIndex, u8 unlockedCount, const bool *completed);

void Menu_setVisible(bool visible);

// Advances each planet's orbit position by one frame and repositions its
// sprite; repositions the cursor arrow directly above whichever planet
// selectedIndex (0..MENU_PLANET_COUNT-1, same indexing as main.c's
// sizePresets[]) names, tracking its current (moving) position.
// completed[MENU_PLANET_COUNT] (spec §45, user request: "Los planetas
// que se han completados pintalos de amarillo") -- TRUE paints that
// planet's sprite yellow instead of its usual violet (caller, main.c,
// derives this from its own presetSave[]). Call once per frame while
// the menu is showing.
// unlockedCount (user request: only the first few planets exist to begin
// with, the rest open up with progress) -- planets at or past that index
// are drawn in grey instead of their own colour (user request: "enseña
// los planetas bloqueados pero en gris"), so the ring shows what is still
// to come rather than hiding it. selectedIndex is always below it, main.c's
// own menu navigation never lets the cursor out of the unlocked range.
void Menu_update(u8 selectedIndex, u8 unlockedCount, const bool *completed);

#endif
