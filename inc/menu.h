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
// passed straight into Menu_update() as the selected planet index. 7
// planets (spec §33, user request: 4 new smaller/fewer-letter phases
// added ahead of the original 3) -- only 4 discrete sprite sizes exist
// on real hardware (1-4 tiles/side), so menu.c reuses them across
// multiple planets; the orbit radius (closer = earlier/easier) is the
// primary visual progression now, not sprite size alone.
#define MENU_PLANET_COUNT 7

// Uploads the menu's sprite graphics/palettes to VRAM/CRAM: planets and
// cursor on PAL3 (unused by anything else in the game, spec §31's note
// on this), and the sun sprite on PAL1 -- a real circular sprite, not a
// BG-tile fill (spec §32septies), since BG tiles are too coarse (8x8) to
// read as round at this size. Call once at boot.
void Menu_loadGraphics(void);

// Shows/hides the planet sprites, the cursor arrow and the sun (spec
// §31, §32septies) -- call with TRUE when entering the menu, FALSE when
// leaving it (both ways: starting a game, or the reset combo bringing
// the menu back). Also flips the sun's color between white (visible)
// and playerShip's own violet (hidden) on PAL1's shared slot -- see
// SUN_INK_INDEX in menu.c.
void Menu_setVisible(bool visible);

// Advances each planet's orbit position by one frame and repositions its
// sprite; repositions the cursor arrow directly above whichever planet
// selectedIndex (0..MENU_PLANET_COUNT-1, same indexing as main.c's
// sizePresets[]) names, tracking its current (moving) position. Call
// once per frame while the menu is showing.
void Menu_update(u8 selectedIndex);

#endif
