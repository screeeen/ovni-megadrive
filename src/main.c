#include <genesis.h>
#include "resources.h"
#include "maze.h"
#include "player.h"
#include "guidemap.h"
#include "enemy.h"
#include "items.h"
#include "menu.h"

// STATE_WIN (spec §34): reached by walking back out through the
// insertion link once every letter is collected -- a static screen
// waiting for BUTTON_A, same "hold the state until confirmed" idea as
// STATE_MENU already uses.
typedef enum { STATE_MENU, STATE_PLAYING, STATE_WIN } GameState;

// Control scheme (spec §40-44, user request: wants to try several,
// cycled with a debug combo -- spec §43). Five so far:
// - CONTROL_NORMAL: each D-pad direction moves the ship that way
//   directly -- a fresh press sets it, constant speed from then on
//   (spec §41), not held-to-move. Stops (doesn't bounce) at a wall.
//   The default.
// - CONTROL_DRUNK: the control this game always had before the §40
//   rework (the original js13k scheme) -- the ship auto-moves every
//   frame in whatever direction it's currently facing, bouncing off
//   walls, and LEFT/RIGHT only rotate that facing 90 degrees instead
//   of moving directly. Named for how disorienting it feels compared
//   to direct control.
// - CONTROL_THRUST ("asteroids sin rebote"): LEFT/RIGHT rotate like
//   DRUNK -- repeatedly while held, not just once per press (spec §44,
//   user request: "creo que la nave tiene que rotar tipo asteroids"),
//   so holding it down keeps spinning the facing around, closer to
//   Asteroids' continuous turn than a single 90-degree tap -- but the
//   ship only moves while UP is actually held (real thrust, not
//   auto-forward), and stops instead of bouncing at a wall, like
//   NORMAL.
// - CONTROL_INERTIA: same direct D-pad control as NORMAL, but speed
//   ramps up over a few frames after a fresh direction press instead
//   of jumping straight to full speed (spec §44: widened ramp/ceiling,
//   user request: "inercia tiene que tener más umbral de
//   acceleracion"), for a heavier "real ship" feel.
// - CONTROL_TOMB (spec §44, user request: "un modo que sea como tomb
//   of the mask, se mueve hasta las paredes muy rapido"): same direct
//   D-pad control as NORMAL, but at an extremely high sub-step speed
//   -- a single press slides the ship almost instantly all the way to
//   the next wall, exactly like that game's signature move.
// Starts in NORMAL; persists across games/menu visits (not reset per
// newGame()) since it's a player preference, not part of any one
// run's state.
typedef enum { CONTROL_NORMAL, CONTROL_DRUNK, CONTROL_THRUST, CONTROL_INERTIA, CONTROL_TOMB, CONTROL_MODE_COUNT } ControlMode;
static ControlMode controlMode = CONTROL_NORMAL;

// Every mode but DRUNK moves faster than its original 1px/frame (spec
// §42/§44, user request: "que vaya más rápido en el modo normal") --
// implemented as Player_updateRoom repeating its untouched 1px-step
// logic this many times per visual frame (see its own doc comment for
// why a bigger single jump isn't safe), not as a bigger jump.
// TOMB_MODE_SPEED (spec §44) is deliberately huge -- comfortably more
// sub-steps than the longest straight stretch possible in a MAZE_W x
// MAZE_H room (20x14 cells * 16px -- 250 sub-steps covers that with
// room to spare even along the longer axis) -- so a press reaches the
// next wall (or door) within one or two visual frames, reading as an
// instant slide.
#define NORMAL_MODE_SPEED 3
#define DRUNK_MODE_SPEED 1
#define THRUST_MODE_SPEED 3
#define INERTIA_MAX_SPEED 4
#define TOMB_MODE_SPEED 250

// CONTROL_INERTIA's ramp state (spec §43/§44): speed climbs by 1 each
// frame the ship keeps moving in the SAME direction, up to
// INERTIA_MAX_SPEED, and snaps back down to 1 the instant a NEW
// direction is pressed (or to 0 when nothing has ever been pressed
// yet) -- deliberately simple (no decay on hitting a wall; a debug
// scheme to try out, not a finished physics model).
static u8 inertiaSpeed;
static u8 inertiaLastDir = DIR_NONE;

// CONTROL_THRUST's repeat-rotate state (spec §44): holding LEFT/RIGHT
// keeps rotating every THRUST_ROTATE_REPEAT_FRAMES frames instead of
// just once per press, closer to Asteroids' continuous turn than a
// single 90-degree tap (this game's collision is cardinal-direction
// only, so true free-angle rotation isn't on the table -- this is the
// closest fit within that constraint).
#define THRUST_ROTATE_REPEAT_FRAMES 8
static u16 thrustRotateTimer;

typedef struct { u8 cols, rows, letters; } SizePreset;

// 4 new smaller/shorter phases (spec §33, user request: "fases mas
// pequeñas, con 1,2,3,4 letras y grids mas pequeñas") ahead of the
// original 3 (always 5 letters, spec §0/§32bis) -- letters is the new
// per-preset item count (guidemap.h's itemCount, no longer a fixed
// ITEM_COUNT=5 for every game). Fuzz-tested (host-side, 20000 seeds per
// preset) via a full simulated playthrough of each -- see spec §33.
static const SizePreset sizePresets[] = {
    { 3, 3, 1 },
    { 4, 3, 2 },
    { 5, 3, 3 },
    { 5, 4, 4 },
    { 6, 4, 5 },
    { 8, 6, 5 },
    { 10, 8, 5 },
};
// Must equal menu.h's MENU_PLANET_COUNT (spec §31) -- sizePresetIndex
// (0..SIZE_PRESET_COUNT-1) is passed straight into Menu_update() as the
// selected planet index, indexing menu.c's own per-planet arrays.
#define SIZE_PRESET_COUNT 7
#define SIZE_PRESET_DEFAULT 4 // 6x4/5 letters, the original smallest full preset (§0)

#define ENEMY_COUNT 2

// playerShip's palette (spec §13bis) is 2 colors: index0 (never rendered
// -- Genesis sprite hardware always treats palette index0 as transparent)
// and index1, the ship's actual visible color. mapShip (spec §24, an 8x8
// single-tile silhouette, same size as a guide-map letter) shares PAL1
// too -- same 2 colors, so no separate palette load needed for it.
// Absolute CRAM index of PAL1's index1 (spec §23): used to flash
// whichever of the two sprites is showing white on the guide map,
// without touching shape/tiles or any other palette.
#define PLAYER_SHIP_INK_INDEX ((PAL1 * 16) + 1)

// Blink period while the guide map is open (spec §23, slowed down for
// spec §24): toggled every this many frames, so a full on/off cycle is
// 2x this at 60fps.
#define MAP_BLINK_FRAMES 30

static Player player;
static Sprite *playerSprite;
static Sprite *mapShipSprite;
static Enemy enemies[ENEMY_COUNT];
static Sprite *enemySprites[ENEMY_COUNT];
static u16 mapSeed;
static u8 currentCol, currentRow;
// TRUE while the player is still in the special insertion room (spec
// §27), before currentCol/currentRow are ever set -- gates every place
// that would otherwise read a stale/meaningless (currentCol,currentRow).
static bool inInsertRoom;
// Which border the insertion room's mission door sits on this game
// (spec §29quat: derived once per newGame() as the OPPOSITE side of
// insertLinkDir -- N<->S, E<->W -- so leaving the insertion room and
// arriving at the periphery room reads as one continuous, spatially
// consistent line, same as any normal room-to-room transition; was
// independently randomized before that, spec §29ter). A DOOR_N/E/S/W
// value (guidemap.h).
static u8 insertRoomDoorDir;
// The insertion room's SECOND door (spec §36, user request: "tiene que
// haber una salida en la sala de extracción para que el usuario vuelva
// a salir al menu") -- always perpendicular to insertRoomDoorDir (never
// the same or opposite side, so it can never collide with the mission
// door), derived once per newGame() alongside it. Walking through it
// always returns straight to the menu (resetToMenu(), which already
// saves progress -- spec §35), regardless of how much of the run is
// done, unlike the mission door whose outcome depends on progress.
// menuDoorOffset is fixed/centered (MAZE_W/2 for N/S, MAZE_H/2 for E/W)
// -- it never needs to line up with another room's opening the way the
// mission door's offset does, so there's nothing to randomize per game.
static u8 menuDoorDir;
static u8 menuDoorOffset;
static bool mapViewOpen;
static u16 mapBlinkTimer;
static GameState gameState;
static u8 sizePresetIndex = SIZE_PRESET_DEFAULT;

// Per-planet saved progress (spec §35, user request: "cada planeta tenga
// el recuento de sus letras obtenidas... si el player sale de un
// planeta, esas letras se conservan"). Zero-initialized (hasSave=FALSE
// for all) until the ship actually leaves a planet mid-run. mapSeed is
// the piece that lets a resumed game regenerate the EXACT same map:
// GuideMap_generate() pulls from the shared random() stream, which
// (unlike each room's own layout, maze.c's Maze_generateRoom already
// calls setRandomSeed(roomSeed) for that) was never itself reseeded from
// mapSeed before -- newGame() below now does that explicitly on both a
// fresh start and a resume, so replaying the same mapSeed reproduces the
// same room tree/item placement/insertion link deterministically, same
// idea as maze.c already relies on at the per-room level. collectedCount
// is enough to restore item state too (Items_fastForward) since
// collection is always strictly in order (spec §13) -- never a subset.
typedef struct { bool hasSave; u16 mapSeed; u8 collectedCount; } PresetSave;
static PresetSave presetSave[SIZE_PRESET_COUNT];

// Deterministic per-room seed (spec §5): same (col,row) under the same
// mapSeed always yields the same roomSeed, so Maze_generateRoom's layout
// persists across re-entries without ever storing it.
static u16 roomSeedFor(u8 col, u8 row)
{
    u32 h = mapSeed;

    h = h * 374761393u + col;
    h = h * 668265263u + row;
    h ^= h >> 15;

    return (u16) h;
}

// EXIT_NORTH/EAST/SOUTH/WEST (player.h) and DOOR_N/E/S/W (guidemap.h) are
// different enumerations (1/2/3/4 vs 0/1/2/3) for the same 4 directions --
// this converts one to the other, needed wherever insertLinkDir (a
// DOOR_x) has to be compared against Player_updateRoom's return value (an
// EXIT_x), spec §29.
static u8 exitDirForDoorDir(u8 doorDir)
{
    switch (doorDir)
    {
        case DOOR_N: return EXIT_NORTH;
        case DOOR_E: return EXIT_EAST;
        case DOOR_S: return EXIT_SOUTH;
        default:     return EXIT_WEST; // DOOR_W
    }
}

// Places the player just inside the border used to enter the room they
// were just loaded into, aligned with that door's actual span (spec §29,
// offset-aware per spec §30) -- used for the insertion-link transition,
// which (unlike enterRoomFrom) isn't stepping to a grid-adjacent
// (col,row), so there's no exitDir to derive this from the usual way.
// "Entering via DOOR_N" is the same physical scenario as enterRoomFrom's
// EXIT_SOUTH case (left the previous room south, entered this one's
// north side), and so on around -- this mirrors that same placement
// table, just indexed by the new room's own entry side instead of the
// old room's exit side. offset is the door's column (N/S) or row (E/W),
// spec §30 -- no longer always MAZE_DOOR_COL/MAZE_DOOR_ROW.
static void positionPlayerEnteringViaDoorDir(u8 doorDir, u8 offset)
{
    switch (doorDir)
    {
        case DOOR_N:
            player.y = MAZE_TILE_PX;
            player.x = offset * MAZE_TILE_PX;
            break;
        case DOOR_S:
            player.y = MAZE_TILE_PX * (MAZE_H - 2);
            player.x = offset * MAZE_TILE_PX;
            break;
        case DOOR_E:
            player.x = MAZE_TILE_PX * (MAZE_W - 2);
            player.y = offset * MAZE_TILE_PX;
            break;
        default: // DOOR_W
            player.x = MAZE_TILE_PX;
            player.y = offset * MAZE_TILE_PX;
            break;
    }
}

// Points at the mission door (insertRoomDoorDir/insertLinkOffset) from
// inside the insertion room itself (spec §37, user request: "quiero que
// indiques con una flecha la salida dentro de esta habitación") -- a
// plain ASCII arrow glyph on BG_A, no new art needed, same "reuse
// VDP_drawText" approach every other in-room label in this game already
// uses (item letters, HUD). Placed just inside the door's own 2-maze-
// cell-deep opening (the row/column closest to the room's interior),
// centered across its 2-cell width using the same insertLinkOffset the
// door itself is built from (spec §29quat/§30 -- opposite directions
// share the axis, so this is the exact same value already used
// elsewhere for this door's own position). Only ever drawn for the
// MISSION door, not the menu-exit door (spec §36) -- that one is a
// secondary/optional action, this is the one every run needs to find.
static void drawInsertRoomArrow(void)
{
    char s[2] = { 0, '\0' };
    u16 tx, ty;

    switch (insertRoomDoorDir)
    {
        case DOOR_N:
            s[0] = '^';
            tx = (u16) ((insertLinkOffset * 2) + 1);
            ty = 3;
            break;
        case DOOR_S:
            s[0] = 'v';
            tx = (u16) ((insertLinkOffset * 2) + 1);
            ty = (MAZE_H * 2) - 4;
            break;
        case DOOR_E:
            s[0] = '>';
            tx = (MAZE_W * 2) - 4;
            ty = (u16) ((insertLinkOffset * 2) + 1);
            break;
        default: // DOOR_W
            s[0] = '<';
            tx = 3;
            ty = (u16) ((insertLinkOffset * 2) + 1);
            break;
    }

    VDP_drawText(s, tx, ty);
}

// Insertion-room status line, drawn on BG_B row 2 (row 0 is the FPS
// readout, row 1 is Items_drawHud's letter tracker -- both already
// high-priority so they show over BG_A's maze) -- shown whenever the
// ship is in the insertion room (spec §34, user request: "distinguir
// salida incompleta vs completa"). The ship can walk back out through
// the link at any time regardless of progress (unchanged, always could);
// this just makes it visible, right where that choice is made, that
// there's still something left uncollected. Never shown once the phase
// is actually complete -- that case shows the full victory screen
// instead (see the STATE_WIN transition below), it never lingers here.
#define INSERT_STATUS_ROW 2
static void drawInsertRoomStatus(bool show)
{
    VDP_setTextPriority(1);
    if (show)
        VDP_drawTextBG(BG_B, "AUN FALTAN LETRAS", 1, INSERT_STATUS_ROW);
    else
        VDP_clearTextLineBG(BG_B, INSERT_STATUS_ROW);
    VDP_setTextPriority(0);
}

// Offset (spec §30) of (col,row)'s door in direction dir -- transparently
// covers the insertion link's extra door too (spec §29): when (col,row)
// is (insertLinkCol,insertLinkRow) and dir is insertLinkDir, that edge
// isn't a real tree door so GuideMap_doorOffset wouldn't have a
// meaningful value for it; insertLinkOffset is used instead. Every other
// case reads the real per-edge value guidemap.c already guarantees
// matches on both sides of that door.
static u8 doorOffsetFor(u8 col, u8 row, u8 dir)
{
    if ((col == insertLinkCol) && (row == insertLinkRow) && (dir == insertLinkDir))
        return insertLinkOffset;

    return GuideMap_doorOffset(col, row, dir);
}

static void loadRoom(u8 col, u8 row, u8 entryDir)
{
    const MapCell cell = guideMap[row][col];
    const u16 seed = roomSeedFor(col, row);
    // spec §46: which door leads toward whatever the ship actually
    // needs next, so Maze_generateRoom can guarantee a TOMB-safe path
    // between it and entryDir (wherever this load is entering from).
    const u8 criticalDir = GuideMap_criticalDoorDir(col, row);
    // Locked branches (spec §16): a door that exists in the room graph but
    // leads only to letters not due yet gets sealed -- short-circuit skips
    // GuideMap_isRoomLocked when there's no door at all in that direction,
    // so out-of-range neighbor coords are never read. Gated on cell.doorX
    // (the RAW tree bit), never the insertLinkDir-merged doorX below --
    // the insertion link is never part of the lock graph.
    const bool lockedN = cell.doorN && GuideMap_isRoomLocked(col, row - 1);
    const bool lockedE = cell.doorE && GuideMap_isRoomLocked(col + 1, row);
    const bool lockedS = cell.doorS && GuideMap_isRoomLocked(col, row + 1);
    const bool lockedW = cell.doorW && GuideMap_isRoomLocked(col - 1, row);
    const u8 sectionHue = GuideMap_roomSection(col, row); // spec §18: per-branch wall color
    // The insertion room's link (spec §27/§29) punches an extra, always-
    // unlocked door on whichever border side has no grid neighbor at all
    // -- guideMap's own doorN/E/S/W bits are left untouched (BFS/locks/
    // sections/corridor-drawing in guidemap.c never see this), it only
    // affects what gets physically carved into THIS room's own maze.
    const bool isInsertLinkRoom = (col == insertLinkCol) && (row == insertLinkRow);
    const bool doorN = cell.doorN || (isInsertLinkRoom && (insertLinkDir == DOOR_N));
    const bool doorE = cell.doorE || (isInsertLinkRoom && (insertLinkDir == DOOR_E));
    const bool doorS = cell.doorS || (isInsertLinkRoom && (insertLinkDir == DOOR_S));
    const bool doorW = cell.doorW || (isInsertLinkRoom && (insertLinkDir == DOOR_W));
    // Where each of those doors sits along its border (spec §30) --
    // array indices match guidemap.h's DOOR_N/E/S/W numbering (0/1/2/3),
    // same convention maze.c's doorOffsets[4] parameter expects.
    const u8 doorOffsets[4] = {
        doorOffsetFor(col, row, DOOR_N),
        doorOffsetFor(col, row, DOOR_E),
        doorOffsetFor(col, row, DOOR_S),
        doorOffsetFor(col, row, DOOR_W),
    };
    u8 i;

    Maze_generateRoom(doorN, doorE, doorS, doorW,
                       lockedN, lockedE, lockedS, lockedW, doorOffsets, sectionHue, seed,
                       entryDir, criticalDir);
    Maze_draw();
    Items_drawInRoom(col, row);

    guideMap[row][col].visited = TRUE;

    // Same seed the room's maze uses, so the patrol routes are
    // deterministic per room too (spec-equivalent persistence, see
    // enemy.h). Each enemy draws further from the same reseeded stream,
    // so the two land on different tiles in practice without needing to
    // coordinate explicitly.
    for (i = 0; i < ENEMY_COUNT; i++)
    {
        Enemy_spawnForRoom(&enemies[i], seed);
        SPR_setPosition(enemySprites[i], enemies[i].x, enemies[i].y);
    }
}

// Room transition (spec §7): move to the neighboring cell, regenerate its
// (deterministic) layout, and place the player just inside the opposite
// border, aligned with that door's actual span (spec §30), still heading
// the same direction.
static void enterRoomFrom(u8 exitDir)
{
    u8 enterDoorDir;

    switch (exitDir)
    {
        case EXIT_NORTH: currentRow--; enterDoorDir = DOOR_S; break;
        case EXIT_EAST:  currentCol++; enterDoorDir = DOOR_W; break;
        case EXIT_SOUTH: currentRow++; enterDoorDir = DOOR_N; break;
        default:         currentCol--; enterDoorDir = DOOR_E; break; // EXIT_WEST
    }

    loadRoom(currentCol, currentRow, enterDoorDir);
    positionPlayerEnteringViaDoorDir(enterDoorDir, doorOffsetFor(currentCol, currentRow, enterDoorDir));
}

static void newGame(void)
{
    const PresetSave save = presetSave[sizePresetIndex];

    mapViewOpen = FALSE;
    // Resume this planet's saved map if it has one (spec §35), otherwise
    // draw a fresh seed from the ongoing global stream same as always.
    mapSeed = save.hasSave ? save.mapSeed : random();
    // Reseed explicitly from mapSeed so GuideMap_generate()'s own
    // random() sequence becomes reproducible from mapSeed alone (see the
    // presetSave doc comment above) -- needed for a resume to regenerate
    // the identical map, and harmless on a fresh start (mapSeed was
    // itself just drawn from the same stream one line up).
    setRandomSeed(mapSeed);

    mapCols = sizePresets[sizePresetIndex].cols;
    mapRows = sizePresets[sizePresetIndex].rows;
    itemCount = sizePresets[sizePresetIndex].letters; // spec §33

    GuideMap_generate();
    Items_reset();
    if (save.hasSave)
        Items_fastForward(save.collectedCount); // spec §35 -- restore prior progress on this planet
    GuideMap_recomputeLocks(); // unlocks up through whichever letter is now due (spec §16)

    // The player's actual physical starting point is the special
    // insertion room (spec §27), outside the grid entirely -- NOT
    // (startCol,startRow), which stays the room tree's logical root
    // (BFS/lock/section origin) and is otherwise unrelated to where the
    // ship first appears. currentCol/currentRow are only set once the
    // player leaves the insertion room, into (insertLinkCol,insertLinkRow).
    inInsertRoom = TRUE;
    // Which border its own door sits on: the OPPOSITE side of
    // insertLinkDir (spec §29quat, N<->S / E<->W -- same "+2 mod 4" flip
    // guidemap.c's own static opposite() and enemy.c's Enemy_opposite()
    // use for the same 4-direction pairing), so the insertion room's
    // exit and the periphery room's entrance read as one continuous
    // line instead of two independently-facing doors. GuideMap_generate()
    // (just above) already set insertLinkDir for this game. Stays fixed
    // for the rest of this playthrough, reused identically every time
    // the room gets regenerated (initial spawn, and any later trip back
    // into it).
    insertRoomDoorDir = (u8) ((insertLinkDir + 2) & 3);
    // The menu-exit door (spec §36): always perpendicular to
    // insertRoomDoorDir, so it's never on the same or opposite wall as
    // the mission door. Fixed centered offset -- see its own doc comment
    // above for why this never needs to be randomized.
    menuDoorDir = (u8) ((insertRoomDoorDir + 1) & 3);
    menuDoorOffset = ((menuDoorDir == DOOR_N) || (menuDoorDir == DOOR_S)) ? (MAZE_W / 2) : (MAZE_H / 2);
    // Same offset as insertLinkOffset (spec §30): opposite directions
    // (N<->S, E<->W) share the same axis, so no translation is needed.
    Maze_generateInsertionRoom(insertRoomDoorDir, insertLinkOffset, menuDoorDir, menuDoorOffset, mapSeed);
    Maze_draw();
    drawInsertRoomArrow(); // spec §37
    Items_drawHud();
    drawInsertRoomStatus(TRUE); // spec §34 -- always true here, itemCount is always >= 1
    Player_spawnAtRoomCenter(&player);
    // Direct-control modes never auto-move (spec §40/§41/§43/§44) --
    // overrides Player_spawnAtRoomCenter's own DIR_DOWN default (meant
    // for DRUNK's/THRUST's always-a-real-facing behavior) so the ship
    // actually sits still until the player presses a direction for the
    // first time. THRUST doesn't need this: its speed is already 0
    // unless UP is held, regardless of what p->dir starts as.
    if ((controlMode == CONTROL_NORMAL) || (controlMode == CONTROL_INERTIA) || (controlMode == CONTROL_TOMB))
        player.dir = DIR_NONE;
    SPR_setPosition(playerSprite, player.x, player.y);
    SPR_setVisibility(playerSprite, VISIBLE);
    {
        u8 i;
        // No enemies in the insertion room (spec §27) -- they stay
        // hidden until the player reaches their first real room.
        for (i = 0; i < ENEMY_COUNT; i++)
            SPR_setVisibility(enemySprites[i], HIDDEN);
    }
}

// Solar-system start menu (spec §31): each planet is one of
// sizePresets[] (same order, innermost orbit = smallest map). Redrawn
// on every LEFT/RIGHT (like the old text-only menu was) since
// VDP_clearPlane wipes the title/hint text along with everything else on
// BG_A -- the sun and planets are sprites now (spec §32septies), so they
// don't need to be redrawn here at all, only the text.
static void drawMenu(void)
{
    char buf[24];
    int len;
    const u8 letters = sizePresets[sizePresetIndex].letters;
    // Saved progress for the selected planet (spec §35) -- 0 only when
    // it has never been played at all. A finished planet's save keeps
    // collectedCount == letters (spec §38 fix), so it correctly reads as
    // fully complete here instead of resetting back to 0.
    const u8 collected = presetSave[sizePresetIndex].hasSave ? presetSave[sizePresetIndex].collectedCount : 0;

    VDP_clearPlane(BG_A, TRUE);

    // Title moved up and the bottom text pushed down (spec §32quat) to
    // free the extra vertical room the widened orbits need (menu.c).
    VDP_drawText("OVNI", 18, 3);

    // Letter count shown alongside the grid size (spec §33) -- the
    // preset's real difficulty is the combination of both, not the grid
    // size alone, now that they vary independently.
    len = sprintf(buf, "%d x %d - %d %s", sizePresets[sizePresetIndex].cols, sizePresets[sizePresetIndex].rows,
                  letters, (letters == 1) ? "LETRA" : "LETRAS");
    VDP_drawText(buf, (40 - len) / 2, 25);

    // Recogidas/faltan del planeta seleccionado (spec §35, user request):
    // progress is per-planet and survives leaving mid-run (see
    // presetSave), so this reflects that saved state, not the live game.
    len = sprintf(buf, "%d DE %d RECOGIDAS", collected, letters);
    VDP_drawText(buf, (40 - len) / 2, 26);

    VDP_drawText("PULSA A PARA EMPEZAR", 10, 27);
}

// Hard reset combo (user request): A+B+C+UP together, from anywhere
// (menu or mid-game), drops back to the size-select menu. Checked ahead
// of the per-state input handling below so it always takes priority over
// whatever any of those 4 buttons would otherwise do that same frame.
#define RESET_COMBO (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_UP)

// Control-mode debug combo (spec §40, extended in §43 to cycle rather
// than just toggle, per user request: "quiero probar varias... combo
// por ahora"): UP+B+C together, from anywhere, advances to the NEXT
// ControlMode (wrapping back to CONTROL_NORMAL after the last one) --
// a subset of RESET_COMBO's own buttons (missing only A), checked as a
// separate `else if` right after it in the main loop, so holding all 4
// (A+B+C+UP) always resolves as the reset alone, never both at once.
#define DRUNK_TOGGLE_COMBO (BUTTON_B | BUTTON_C | BUTTON_UP)

static void resetToMenu(void)
{
    u8 i;
    const GameState previousState = gameState; // capture before overwriting below

    // Snapshot progress for the planet just left (spec §35, fixed in
    // §38: a finished planet used to clear its save entirely, which
    // showed as "0 DE N" in the menu right after completing it --
    // user-reported bug, should read as fully complete instead). Only
    // when actually leaving a real game (mid-run via the reset combo,
    // or just won), never when the combo is pressed while already at
    // the menu (mapSeed/items.c's collected state would be stale
    // leftovers from whatever was last played, not "this" run).
    // collectedCount naturally equals itemCount when the run was won,
    // which is exactly what should show in the menu.
    if ((previousState == STATE_PLAYING) || (previousState == STATE_WIN))
    {
        presetSave[sizePresetIndex].hasSave = TRUE;
        presetSave[sizePresetIndex].mapSeed = mapSeed;
        presetSave[sizePresetIndex].collectedCount = Items_collectedCount();
    }

    gameState = STATE_MENU;
    mapViewOpen = FALSE;
    inInsertRoom = FALSE; // harmless either way -- newGame() sets it back to TRUE when a new run starts

    SPR_setVisibility(playerSprite, HIDDEN);
    SPR_setVisibility(mapShipSprite, HIDDEN);
    for (i = 0; i < ENEMY_COUNT; i++)
        SPR_setVisibility(enemySprites[i], HIDDEN);

    // Items_drawHud's letter tracker and drawInsertRoomStatus's message
    // (both BG_B, high priority) are only ever refreshed during gameplay
    // -- clear them so a reset mid-game doesn't leave either lingering
    // over the menu (BG_A's own VDP_clearPlane in drawMenu() below only
    // touches BG_A, a separate plane).
    VDP_clearTextLineBG(BG_B, 1);
    VDP_clearTextLineBG(BG_B, INSERT_STATUS_ROW);

    Menu_setVisible(TRUE);
    drawMenu();
}

int main(bool hardReset)
{
    u16 prevState = 0;

    VDP_setPlaneSize(64, 32, TRUE);
    SPR_init();

    Maze_loadGraphics();
    GuideMap_loadGraphics();
    Menu_loadGraphics(); // spec §31 -- must come after both above, its tiles stack right after theirs in VRAM

    PAL_setPalette(PAL1, playerShip.palette->data, DMA);
    playerSprite = SPR_addSprite(&playerShip, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    // mapShip (spec §24): same PAL1, same 2 colors as playerShip, so no
    // separate palette load -- just a smaller (1 tile, 8x8) silhouette
    // shown instead of playerShip while the guide map is open.
    mapShipSprite = SPR_addSprite(&mapShip, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));

    PAL_setPalette(PAL2, enemyShip.palette->data, DMA);
    {
        u8 i;
        for (i = 0; i < ENEMY_COUNT; i++)
            enemySprites[i] = SPR_addSprite(&enemyShip, 0, 0, TILE_ATTR(PAL2, TRUE, FALSE, FALSE));
    }

    JOY_init();

    gameState = STATE_MENU;
    SPR_setVisibility(playerSprite, HIDDEN);
    SPR_setVisibility(mapShipSprite, HIDDEN);
    {
        u8 i;
        for (i = 0; i < ENEMY_COUNT; i++)
            SPR_setVisibility(enemySprites[i], HIDDEN);
    }
    Menu_setVisible(TRUE);
    drawMenu();

    while (TRUE)
    {
        const u16 state = JOY_readJoypad(JOY_1);

        if (((state & RESET_COMBO) == RESET_COMBO) && ((prevState & RESET_COMBO) != RESET_COMBO))
        {
            resetToMenu();
        }
        else if (((state & DRUNK_TOGGLE_COMBO) == DRUNK_TOGGLE_COMBO) && ((prevState & DRUNK_TOGGLE_COMBO) != DRUNK_TOGGLE_COMBO))
        {
            controlMode = (ControlMode) ((controlMode + 1) % CONTROL_MODE_COUNT);
            inertiaSpeed = 0;
            inertiaLastDir = DIR_NONE; // fresh ramp if CONTROL_INERTIA is entered next
        }
        else if (gameState == STATE_MENU)
        {
            // Cursor arrow tracks the selected planet's live orbit
            // position (spec §31) -- advanced every frame regardless of
            // input, same as the enemies keep patrolling during
            // gameplay. completed[] (spec §45, user request: "Los
            // planetas que se han completados pintalos de amarillo")
            // is derived fresh each frame from presetSave[] -- cheap
            // (SIZE_PRESET_COUNT checks) and avoids needing to hook into
            // every place presetSave[] can change.
            bool completed[SIZE_PRESET_COUNT];
            u8 i;

            for (i = 0; i < SIZE_PRESET_COUNT; i++)
                completed[i] = presetSave[i].hasSave && (presetSave[i].collectedCount >= sizePresets[i].letters);

            Menu_update(sizePresetIndex, completed);

            if ((state & BUTTON_LEFT) && !(prevState & BUTTON_LEFT))
            {
                sizePresetIndex = (sizePresetIndex + SIZE_PRESET_COUNT - 1) % SIZE_PRESET_COUNT;
                drawMenu();
            }
            if ((state & BUTTON_RIGHT) && !(prevState & BUTTON_RIGHT))
            {
                sizePresetIndex = (sizePresetIndex + 1) % SIZE_PRESET_COUNT;
                drawMenu();
            }
            if ((state & BUTTON_A) && !(prevState & BUTTON_A))
            {
                Menu_setVisible(FALSE);
                gameState = STATE_PLAYING;
                newGame();
            }
        }
        else if (gameState == STATE_WIN) // spec §34
        {
            if ((state & BUTTON_A) && !(prevState & BUTTON_A))
                resetToMenu();
        }
        else // STATE_PLAYING
        {
            // bounceMode/moveSpeed (spec §43) are computed ONCE per
            // frame here, from whichever ControlMode is active, and read
            // by BOTH Player_updateRoom call sites below (one for the
            // insertion room, one for a real room -- only one of those
            // branches ever runs a given frame, but both need these).
            bool bounceMode;
            u8 moveSpeed;

            if (controlMode == CONTROL_DRUNK)
            {
                // Edge-triggered, rotate once per press -- the original
                // js13k feel, deliberately left untouched. Inverted as a
                // test (user request): LEFT turns counter-clockwise,
                // RIGHT clockwise -- swapped from the original mapping
                // below.
                if ((state & BUTTON_LEFT) && !(prevState & BUTTON_LEFT))
                    Player_rotateCCW(&player);
                if ((state & BUTTON_RIGHT) && !(prevState & BUTTON_RIGHT))
                    Player_rotateCW(&player);
            }
            else if (controlMode == CONTROL_THRUST)
            {
                // Repeat-rotate while held (spec §44, user request:
                // "creo que la nave tiene que rotar tipo asteroids") --
                // rotates on the initial press, then again every
                // THRUST_ROTATE_REPEAT_FRAMES frames for as long as the
                // same button stays held, instead of needing a fresh
                // tap each time. Cardinal-direction quantized either way
                // (this game's collision only supports 4 directions),
                // but holding down a turn now keeps spinning through
                // them, closer to Asteroids' continuous rotation than a
                // single 90-degree step per press.
                if (state & BUTTON_LEFT)
                {
                    if (!(prevState & BUTTON_LEFT) || (++thrustRotateTimer >= THRUST_ROTATE_REPEAT_FRAMES))
                    {
                        Player_rotateCCW(&player);
                        thrustRotateTimer = 0;
                    }
                }
                else if (state & BUTTON_RIGHT)
                {
                    if (!(prevState & BUTTON_RIGHT) || (++thrustRotateTimer >= THRUST_ROTATE_REPEAT_FRAMES))
                    {
                        Player_rotateCW(&player);
                        thrustRotateTimer = 0;
                    }
                }
                else
                {
                    thrustRotateTimer = 0;
                }
            }
            else // CONTROL_NORMAL, CONTROL_INERTIA or CONTROL_TOMB (spec
                 // §40/§41/§44): each D-pad direction sets the ship's
                 // facing directly -- edge-triggered (a NEW press, not
                 // held), so once set it keeps going until a fresh press
                 // of a DIFFERENT direction redirects it, or a wall stops
                 // it in place (movePlayer's bounceOnWall=FALSE). No
                 // rotation. Priority order when more than one is newly
                 // pressed the same frame (no diagonals): UP, DOWN, LEFT,
                 // RIGHT.
            {
                if ((state & BUTTON_UP) && !(prevState & BUTTON_UP)) player.dir = DIR_UP;
                else if ((state & BUTTON_DOWN) && !(prevState & BUTTON_DOWN)) player.dir = DIR_DOWN;
                else if ((state & BUTTON_LEFT) && !(prevState & BUTTON_LEFT)) player.dir = DIR_LEFT;
                else if ((state & BUTTON_RIGHT) && !(prevState & BUTTON_RIGHT)) player.dir = DIR_RIGHT;
            }

            switch (controlMode)
            {
                case CONTROL_DRUNK:
                    bounceMode = TRUE;
                    moveSpeed = DRUNK_MODE_SPEED;
                    break;
                case CONTROL_THRUST:
                    // "Asteroids sin rebote": only actually moves while
                    // UP is HELD (real thrust, not auto-forward like
                    // DRUNK) -- speed 0 makes Player_updateRoom's
                    // sub-step loop run zero times, doing nothing at all
                    // this frame (no movement, no exit check either --
                    // can't drift through a door while not thrusting).
                    bounceMode = FALSE;
                    moveSpeed = (state & BUTTON_UP) ? THRUST_MODE_SPEED : 0;
                    break;
                case CONTROL_INERTIA:
                    // Ramps speed up while continuing the same direction,
                    // snaps down to 1 on a fresh direction press, 0 once
                    // nothing has ever been pressed.
                    bounceMode = FALSE;
                    if (player.dir == DIR_NONE)
                        inertiaSpeed = 0;
                    else if (player.dir != inertiaLastDir)
                        inertiaSpeed = 1;
                    else if (inertiaSpeed < INERTIA_MAX_SPEED)
                        inertiaSpeed++;
                    inertiaLastDir = player.dir;
                    moveSpeed = inertiaSpeed;
                    break;
                case CONTROL_TOMB:
                    // "Tomb of the Mask" (spec §44): TOMB_MODE_SPEED sub-
                    // steps per frame slides the ship almost instantly
                    // all the way to the next wall or door, same as that
                    // game's signature move -- stops there (bounceMode
                    // FALSE) rather than bouncing back.
                    bounceMode = FALSE;
                    moveSpeed = (player.dir == DIR_NONE) ? 0 : TOMB_MODE_SPEED;
                    break;
                default: // CONTROL_NORMAL
                    bounceMode = FALSE;
                    moveSpeed = NORMAL_MODE_SPEED;
                    break;
            }

            // Map view disabled while still in the insertion room (spec
            // §27) -- currentCol/currentRow aren't set yet, and there's
            // nothing to preview before the player has even entered the
            // grid.
            if (!inInsertRoom && (state & BUTTON_C) && !(prevState & BUTTON_C))
            {
                mapViewOpen = !mapViewOpen;

                if (mapViewOpen)
                {
                    u8 i;
                    u16 mx, my;

                    GuideMap_drawOverlay();

                    // mapShip (spec §24, 8x8, same size as a map letter)
                    // marks the current room instead of playerSprite
                    // (which hides, same as the enemies) -- centered in
                    // the 24x16px room box: (24-8)/2=8 horizontally,
                    // (16-8)/2=4 vertically. White and blinking (spec
                    // §23): flip the shared ink color, start visible,
                    // reset the blink timer -- the per-frame toggle below
                    // picks it up from here.
                    GuideMap_roomBoxPixelPos(currentCol, currentRow, &mx, &my);
                    SPR_setPosition(mapShipSprite, mx + 8, my + 4);
                    PAL_setColor(PLAYER_SHIP_INK_INDEX, RGB24_TO_VDPCOLOR(0xFFFFFF));
                    SPR_setVisibility(playerSprite, HIDDEN);
                    SPR_setVisibility(mapShipSprite, VISIBLE);
                    mapBlinkTimer = 0;

                    for (i = 0; i < ENEMY_COUNT; i++)
                        SPR_setVisibility(enemySprites[i], HIDDEN);
                }
                else
                {
                    u8 i;

                    Maze_draw();
                    // Restores the ship's normal color (spec §23) -- only
                    // the one word that PLAYER_SHIP_INK_INDEX touched,
                    // the transparent index0 was never changed.
                    PAL_setColor(PLAYER_SHIP_INK_INDEX, playerShip.palette->data[1]);
                    SPR_setVisibility(playerSprite, VISIBLE);
                    SPR_setVisibility(mapShipSprite, HIDDEN);
                    for (i = 0; i < ENEMY_COUNT; i++)
                        SPR_setVisibility(enemySprites[i], VISIBLE);
                }
            }

            if (mapViewOpen)
            {
                // Blink mapShip on the map (spec §23/§24): flip
                // visibility every MAP_BLINK_FRAMES frames while the
                // overlay stays open. Position doesn't need re-setting
                // each frame -- it's static while viewing the map.
                mapBlinkTimer++;
                if (mapBlinkTimer >= MAP_BLINK_FRAMES)
                {
                    mapBlinkTimer = 0;
                    SPR_setVisibility(mapShipSprite, SPR_isVisible(mapShipSprite, FALSE) ? HIDDEN : VISIBLE);
                }
            }
            else if (inInsertRoom)
            {
                // The insertion room has two doors (spec §27/§36):
                // insertRoomDoorDir (the mission door, spec §29ter) and
                // menuDoorDir (perpendicular to it by construction, so
                // never the same side) -- no items/enemies/locks apply
                // here, it lives outside the normal grid entirely.
                const bool doorN = (insertRoomDoorDir == DOOR_N) || (menuDoorDir == DOOR_N);
                const bool doorE = (insertRoomDoorDir == DOOR_E) || (menuDoorDir == DOOR_E);
                const bool doorS = (insertRoomDoorDir == DOOR_S) || (menuDoorDir == DOOR_S);
                const bool doorW = (insertRoomDoorDir == DOOR_W) || (menuDoorDir == DOOR_W);
                // Each direction is claimed by at most one of the two
                // doors (they're always perpendicular), so exactly one of
                // these two ternary branches is live per slot -- the
                // other is never read since its matching doorX is FALSE.
                const u8 offN = (insertRoomDoorDir == DOOR_N) ? insertLinkOffset : menuDoorOffset;
                const u8 offE = (insertRoomDoorDir == DOOR_E) ? insertLinkOffset : menuDoorOffset;
                const u8 offS = (insertRoomDoorDir == DOOR_S) ? insertLinkOffset : menuDoorOffset;
                const u8 offW = (insertRoomDoorDir == DOOR_W) ? insertLinkOffset : menuDoorOffset;
                const u8 exitDir = Player_updateRoom(&player, bounceMode, moveSpeed,
                                                      doorN, doorE, doorS, doorW, offN, offE, offS, offW);

                if (exitDir == exitDirForDoorDir(insertRoomDoorDir))
                {
                    u8 i;

                    // Jump straight to the chosen perimeter room and land
                    // right at its real border opening (spec §29,
                    // insertLinkDir -- loadRoom() already punched it
                    // open, same call as for any of that room's own tree
                    // doors), aligned with that door's actual span (spec
                    // §30).
                    inInsertRoom = FALSE;
                    currentCol = insertLinkCol;
                    currentRow = insertLinkRow;
                    loadRoom(currentCol, currentRow, insertLinkDir);
                    positionPlayerEnteringViaDoorDir(insertLinkDir, insertLinkOffset);
                    drawInsertRoomStatus(FALSE); // spec §34 -- only relevant while actually in the insertion room

                    for (i = 0; i < ENEMY_COUNT; i++)
                        SPR_setVisibility(enemySprites[i], VISIBLE);
                }
                else if (exitDir == exitDirForDoorDir(menuDoorDir))
                {
                    // Walked out through the menu-exit door (spec §36).
                    // This is now where completing the phase is actually
                    // decided (spec §39, user request: "para completar la
                    // fase, tiene que salir de la habitación de inserción/
                    // extracción con todas las letras recogidas") -- not
                    // merely arriving back at the insertion room from the
                    // grid (see the isInsertLinkRoom branch above, which
                    // no longer checks this at all).
                    if (Items_allCollected())
                    {
                        // Phase complete: victory screen, wait for
                        // BUTTON_A (STATE_WIN, handled in the main
                        // dispatch below) to head back to the menu.
                        u8 i;

                        gameState = STATE_WIN;

                        SPR_setVisibility(playerSprite, HIDDEN);
                        for (i = 0; i < ENEMY_COUNT; i++)
                            SPR_setVisibility(enemySprites[i], HIDDEN); // already hidden here, harmless
                        drawInsertRoomStatus(FALSE);

                        VDP_clearPlane(BG_A, TRUE);
                        VDP_drawText("FASE COMPLETADA", 12, 12);
                        VDP_drawText("PULSA A PARA VOLVER AL MENU", 6, 15);
                    }
                    else
                    {
                        // Not done yet: straight back to the menu as
                        // before -- resetToMenu() saves progress (spec
                        // §35).
                        resetToMenu();
                    }
                }

                SPR_setPosition(playerSprite, player.x, player.y);
            }
            else
            {
                const MapCell cell = guideMap[currentRow][currentCol];
                // The insertion link's extra door (spec §29) is merged in
                // here too, only when this IS that specific room -- by
                // construction insertLinkDir is always a side with no
                // real tree door, so it can never collide with one of
                // cell.doorN/E/S/W below.
                const bool isInsertLinkRoom = (currentCol == insertLinkCol) && (currentRow == insertLinkRow);
                const bool doorN = cell.doorN || (isInsertLinkRoom && (insertLinkDir == DOOR_N));
                const bool doorE = cell.doorE || (isInsertLinkRoom && (insertLinkDir == DOOR_E));
                const bool doorS = cell.doorS || (isInsertLinkRoom && (insertLinkDir == DOOR_S));
                const bool doorW = cell.doorW || (isInsertLinkRoom && (insertLinkDir == DOOR_W));
                const u8 exitDir = Player_updateRoom(&player, bounceMode, moveSpeed,
                                                      doorN, doorE, doorS, doorW,
                                                      doorOffsetFor(currentCol, currentRow, DOOR_N),
                                                      doorOffsetFor(currentCol, currentRow, DOOR_E),
                                                      doorOffsetFor(currentCol, currentRow, DOOR_S),
                                                      doorOffsetFor(currentCol, currentRow, DOOR_W));

                if (exitDir != EXIT_NONE)
                {
                    if (isInsertLinkRoom && (exitDir == exitDirForDoorDir(insertLinkDir)))
                    {
                        // Walked back out through the insertion link, into
                        // the insertion room -- always just that, regardless
                        // of progress (spec §39, user request: completing
                        // the phase requires actually LEAVING the insertion
                        // room afterwards, via its own menu-exit door, not
                        // merely arriving back at it from the grid -- see
                        // that door's own handling in the inInsertRoom
                        // branch below for where the win check now lives).
                        // Same freshly generated insertion room every time
                        // (same insertRoomDoorDir/menuDoorDir as its initial
                        // spawn, spec §29ter/§36 -- not re-randomized),
                        // entering via its mission door, same placement its
                        // own arrival uses. Nothing is left to collect/
                        // patrol on THIS frame (currentCol/currentRow are
                        // now stale, still pointing at the periphery room),
                        // same reason the sibling inInsertRoom branch already
                        // skips that below -- so inInsertRoom flips TRUE.
                        u8 i;

                        inInsertRoom = TRUE;
                        Maze_generateInsertionRoom(insertRoomDoorDir, insertLinkOffset, menuDoorDir, menuDoorOffset, mapSeed);
                        Maze_draw();
                        drawInsertRoomArrow(); // spec §37
                        positionPlayerEnteringViaDoorDir(insertRoomDoorDir, insertLinkOffset);
                        drawInsertRoomStatus(TRUE); // spec §34

                        for (i = 0; i < ENEMY_COUNT; i++)
                            SPR_setVisibility(enemySprites[i], HIDDEN);
                    }
                    else
                    {
                        enterRoomFrom(exitDir);
                    }
                }

                SPR_setPosition(playerSprite, player.x, player.y);

                // Skipped on the one frame that just sent the player back
                // into the insertion room (inInsertRoom flips TRUE above)
                // -- currentCol/currentRow are now stale (still pointing
                // at the periphery room), and there's nothing to collect
                // or patrol in the insertion room anyway.
                if (!inInsertRoom)
                {
                    // Physical contact pickup, order enforced (spec §13):
                    // does nothing unless (currentCol,currentRow) holds
                    // the next letter due AND the ship's box overlaps it.
                    if (Items_tryCollect(currentCol, currentRow, player.x, player.y))
                    {
                        Maze_draw();           // wipes the now-collected letter's tile
                        Items_drawInRoom(currentCol, currentRow); // no-op here, kept for symmetry with loadRoom
                        Items_drawHud();
                        // Unlocks the next branch (spec §16); the current
                        // room's own doors never change from this (items
                        // only live in dead ends), it only affects rooms
                        // not yet loaded -- they pick up the new lock
                        // state next time loadRoom() regenerates them.
                        GuideMap_recomputeLocks();
                    }

                    // Routine patrol, no player interaction yet.
                    {
                        u8 i, j;

                        for (i = 0; i < ENEMY_COUNT; i++)
                            Enemy_update(&enemies[i]);

                        // Bounce off each other: reverse both on overlap.
                        // A brief 1-frame overlap before they separate is
                        // imperceptible at 60fps and there's no damage/
                        // health model yet to make it matter.
                        for (i = 0; i < ENEMY_COUNT; i++)
                            for (j = i + 1; j < ENEMY_COUNT; j++)
                                if (Enemy_overlaps(&enemies[i], &enemies[j]))
                                {
                                    enemies[i].dir = Enemy_opposite(enemies[i].dir);
                                    enemies[j].dir = Enemy_opposite(enemies[j].dir);
                                }

                        for (i = 0; i < ENEMY_COUNT; i++)
                            SPR_setPosition(enemySprites[i], enemies[i].x, enemies[i].y);
                    }
                }
            }
        }

        // FPS debug readout (user request), top-right corner on BG_B --
        // same plane/high-priority trick Items_drawHud uses, so it stays
        // visible over BG_A's low-priority maze/menu tiles without those
        // needing to coordinate with it. SYS_getFPS() must be called
        // exactly once per frame (its own doc comment) -- this is that
        // one call, unconditional regardless of gameState.
        {
            char buf[8];
            int len = sprintf(buf, "FPS%lu", SYS_getFPS());

            VDP_setTextPriority(1);
            VDP_drawTextBG(BG_B, buf, 40 - len - 1, 0);
            VDP_setTextPriority(0);
        }

        // Control-mode readout (spec §40, 5 modes since §44), top-left
        // corner on BG_B, same row/plane/priority trick as the FPS
        // counter above -- lets the player (and, since this session
        // can't take screenshots, the developer) confirm at a glance
        // which mode is active from anywhere, menu included. Every
        // label is padded to 8 chars (BORRACHO's own length) so cycling
        // modes can't leave a stray trailing character from a previous,
        // longer label.
        {
            static const char *const modeLabels[CONTROL_MODE_COUNT] = {
                "NORMAL  ", "BORRACHO", "IMPULSO ", "INERCIA ", "TUMBA   "
            };

            VDP_setTextPriority(1);
            VDP_drawTextBG(BG_B, modeLabels[controlMode], 0, 0);
            VDP_setTextPriority(0);
        }

        prevState = state;

        SPR_update();

        SYS_doVBlankProcess();
    }

    return 0;
}
