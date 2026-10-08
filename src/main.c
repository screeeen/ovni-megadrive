#include <genesis.h>
#include "resources.h"
#include "maze.h"
#include "player.h"
#include "guidemap.h"
#include "items.h"
#include "menu.h"
#include "enemy.h"
#include "sfx.h"
#include "plants.h"

// STATE_GAMEOVER (user request: "un enemigo por habitacion... si te mata
// sale una pantalla game over") -- a static screen waiting for BUTTON_A,
// same "hold the state until confirmed" idea as STATE_MENU already uses,
// reached by touching a DANGEROUS enemy. It used to be deliberately left
// OUT of resetToMenu()'s own progress-save check, so dying threw the
// attempt away; it now saves exactly like walking out does (user
// request), letters, plants, dead enemies and all.
//
// There used to be a STATE_WIN here too (spec §34, the "FASE COMPLETADA"
// screen reached by walking back out through the insertion link with
// every letter collected) -- removed per user request ("elimina la
// pantalla de fase completada, simplemente vuelve al menu"): completing
// the phase now goes straight to resetToMenu(), same as leaving mid-run.
//
// STATE_INTRO (user request: "quiero una pantalla rara al principio del
// programa, que pide al usuario que pulse un boton... como si fuese una
// terminal de un ordenador vintage... durante ese tiempo cargas todos
// los planetas") -- the very first state, before STATE_MENU. Shows a
// static "press a button" prompt (drawIntroPrompt(), main()'s own boot
// code) until the first real press, then introBegin()/introTick() do the
// actual work, one room per frame: picks a galaxy name and, livestreamed
// as scrolling vintage-terminal text, precomputes all SIZE_PRESET_COUNT
// planets (the exact "all 8 at boot" work spec §52 tried once and
// spec §55 reverted, because back then it needed an UNEXPLAINED,
// non-functional button-wait screen just to dodge spec §54's
// random()-before-any-press bug; this time the button-wait is the
// explicit point of the screen, asked for on its own merits, and it
// actually has to work this time).
// STATE_SCORES is the high-score table, which the menu alternates with on
// its own (user request: "haz una tabla high scores que alterne con el
// menu de los planetas"). Added at the end so the debug view's own
// stateNames[] keeps lining up with the values before it.
typedef enum { STATE_INTRO, STATE_MENU, STATE_PLAYING, STATE_GAMEOVER, STATE_SCORES } GameState;

// The ship slides in a straight line until a wall stops it, turning only
// from a stop -- the only control scheme the room generator's slide-graph
// validator (maze.c) guarantees anything for (spec §44).
//
// Tomb slide speed (spec §44, revised: user request "que haya interpolación
// en la animación, que no sea de un frame a otro sino que haya un
// movimiento") used to be 250 -- a whole slide done inside ONE frame, the
// ship just appeared at the far wall. Now it is a plain px-per-frame speed:
// a slide across the 320px room takes ~0.35s at top speed, visibly travelling the
// whole way. Collision/exit/letter checks are unaffected -- every 1px
// sub-step is still checked individually, whatever the speed.
// Ease-in (user request): a slide does not start at full speed, it
// accelerates -- px per frame for the 1st, 2nd, 3rd... frame of a slide,
// then holds the last value (the top speed) until the ship stops. Reaches
// 13 px/frame after 5 frames (~0.08s, ~23px): a short but readable ease-in.
// Tune the values to taste.
static const u8 tombRamp[] = { 2, 4, 7, 10, 13 };
#define TOMB_RAMP_LAST ((u8) (sizeof(tombRamp) - 1))

// The slide state. A slide lasts many frames, so (as in the
// game it is modelled on) the ship cannot be steered while it is moving --
// only from a stop, which is also what the room generator's slide graph
// assumes. tombMoving = the ship changed position last frame; a press
// during a slide is not lost but remembered (tombQueuedDir, only the
// latest) and applied the moment the ship stops.
static bool tombMoving;
static u8 tombQueuedDir = DIR_NONE;
static u8 tombSpeedIdx; // where the current slide is on tombRamp

// cols/rows = how many ROOMS the planet's map has; roomW/roomH/shape =
// how big and what shape each of those rooms is (user request: "cuanto
// mas grande sea el planeta, mas grande es la habitacion... me gustaria
// probar con distintas geometrias"). The room footprint used to be the
// same fixed 20x12 box everywhere; it now grows with the planet, from
// 16x10 on the 1x1 test planet up to the full screen (MAZE_MAX_W x
// MAZE_MAX_H = 40x24) on the outermost one, and each planet gets its own
// shape rolled per room by maze.c (Maze_setRoomSize's last column is how
// many cuts a room may take: 0 = plain boxes).
// Each planet's MAP drawn in ASCII, one string per row: '#' is a room,
// '.' is nothing there. cols/rows below must match these strings. The
// map used to be a solid rectangle always (Prim's filled the whole
// grid); these are the polyomino shapes instead.
static const char *const map0[] = { "#" };                 // una sola sala
static const char *const map1[] = { "####",                // T
                                    ".##.",
                                    ".##." };
static const char *const map2[] = { "##....",              // J
                                    "##....",
                                    "######",
                                    "######" };
static const char *const map3[] = { "######",              // I con remates
                                    "..##..",
                                    "..##..",
                                    "######" };
static const char *const map4[] = { "..###..",             // cruz
                                    "..###..",
                                    "#######",
                                    "..###..",
                                    "..###.." };
static const char *const map5[] = { "#######.",            // O con patas
                                    "##...##.",
                                    "##...###",
                                    "#######.",
                                    "##....##",
                                    "##....##" };
static const char *const map6[] = { "#.####.#",            // O con esquinas
                                    "########",
                                    "##....##",
                                    "##....##",
                                    "########",
                                    "#.####.#" };
static const char *const map7[] = { "##########",          // T grande
                                    "##########",
                                    "....##....",
                                    "....##....",
                                    "....##....",
                                    "..######..",
                                    "..######..",
                                    "..######.." };

typedef struct { const char *const *map; u8 cols, rows, letters, roomW, roomH, variety; } SizePreset;

// 4 new smaller/shorter phases (spec §33, user request: "fases mas
// pequeñas, con 1,2,3,4 letras y grids mas pequeñas") ahead of the
// original 3 (always 5 letters, spec §0/§32bis) -- letters is the new
// per-preset item count (guidemap.h's itemCount, no longer a fixed
// ITEM_COUNT=5 for every game). Fuzz-tested (host-side, 20000 seeds per
// preset) via a full simulated playthrough of each -- see spec §33.
//
// Index 0, { 1, 1, 1 }, is a dedicated tombo test planet (user request:
// "olvidate por ahora de las entradas y salidas, haz 1 planeta con 1
// habitación con el sistema tombo") -- a 1x1 grid, i.e. a single real
// room in the tree. GuideMap_generate() already degrades to this cleanly
// with no special-casing needed: with only one cell, carveTree() marks
// it CELL_ROOM and stops (no neighbors to grow into, so it ends up with
// zero real tree doors), selectInsertionLink() still finds it as a
// perimeter room with all 4 sides free and picks one as insertLinkDir,
// and selectItemRooms() already has a documented fallback for exactly
// this shape ("Not enough dead-end rooms... use the start room for at
// most one item") -- so by the time main.c's loadRoom() actually calls
// Maze_generateRoom for this room, it ends up with exactly ONE active
// door (the insertion link), the same single-target case
// generateRoomTombo already handles like any other. Set as the new
// default planet (SIZE_PRESET_DEFAULT below) so starting a game goes
// straight into it.
static const SizePreset sizePresets[] = {
    { map0,  1, 1, 1, 16, 10, 0 },
    { map1,  4, 3, 1, 20, 12, 1 },
    { map2,  6, 4, 2, 24, 14, 2 },
    { map3,  6, 4, 3, 28, 16, 2 },
    { map4,  7, 5, 4, 30, 18, 3 },
    { map5,  8, 6, 5, 34, 20, 3 },
    { map6,  8, 6, 5, 36, 22, 4 },
    { map7, 10, 8, 5, 40, 24, 4 },
};
// Must equal menu.h's MENU_PLANET_COUNT (spec §31) -- sizePresetIndex
// (0..SIZE_PRESET_COUNT-1) is passed straight into Menu_update() as the
// selected planet index, indexing menu.c's own per-planet arrays.
#define SIZE_PRESET_COUNT 8
#define SIZE_PRESET_DEFAULT 0 // the new 1x1 tombo test planet (user request)

// temporal: solo se cartografian y se pueden elegir los planetas de este
// indice en adelante. 0 = todos, que es el comportamiento normal.
#define DEBUG_FIRST_PLANET 0
#define PLANET_SEL_COUNT (SIZE_PRESET_COUNT - DEBUG_FIRST_PLANET)

// How many planets the BOOT cartography screen maps (user request: "carga
// solo los dos primeros planetas, el resto a demanda cuando se
// seleccionan"). Deliberately NOT PLANETS_AT_START any more: how many are
// playable and how many are worth mapping before the menu even appears
// are different questions, and mapping all eight cost ~2.5x the boot time
// of mapping two. The rest are mapped the first time the cursor lands on
// them, through the same one-room-per-frame screen (see the menu's own
// plantsTotalKnown check), so nothing is ever mapped twice and no planet
// can be entered uncharted.
#define BOOT_CARTOGRAPHY_COUNT 2

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

// One enemy per room (user request), PAL2 (enemyShip's own palette, also
// reused by the menu's "completed planet" yellow -- COMPLETED_INK_INDEX in
// menu.c -- the two never show at once, same sharing the map-ship ink
// index already relies on). Its ink swaps between these two colors as a
// placeholder for the "dangerous"/"vulnerable" states (enemy.h) -- no
// separate sprite art needed, same override trick as PLAYER_SHIP_INK_INDEX.
#define ENEMY_INK_INDEX ((PAL2 * 16) + 1)
#define ENEMY_DANGEROUS_COLOR RGB24_TO_VDPCOLOR(0xFF3030)
#define ENEMY_VULNERABLE_COLOR RGB24_TO_VDPCOLOR(0x30FF90)

// Screenshake (user request: "un ligero screenshake cuando la nave golpea
// muros o enemigos", refined further: "tiene que ser sutil, dura medio
// segundo y medio pixel si puede ser") -- BG_A only (the maze itself),
// never BG_B (the HUD text stays put so it's always readable).
// SHAKE_DURATION_FRAMES = 10 is a sixth of a second at 60fps. Deterministic,
// not true randomness: it comes from shakeFramesLeft's own low bits as it
// counts down, so it needs no draw from the shared random() stream
// (reserved for the map/room/item/enemy generators' own determinism).
#define SHAKE_DURATION_FRAMES 10

// Amplitude scales with how far the ship had been sliding before the
// impact (user request: "la amplitud del screenshake sea proporcional a la
// distancia recorrida de la nave") -- SHAKE_DIST_PER_LEVEL px of travel
// per +1px of peak displacement, capped at SHAKE_MAX_AMPLITUDE so even a
// full-room TOMB slide doesn't throw the camera further than this. A short
// bump right after starting (or right after the last one) stays at the
// original 1px flicker.
#define SHAKE_DIST_PER_LEVEL 24
#define SHAKE_MAX_AMPLITUDE 3

static Player player;
static Sprite *playerSprite;
static Sprite *mapShipSprite;
static Enemy enemy;
static Sprite *enemySprite;
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
// The map screen's own two debug tools (implemented far below, next to
// the room viewer's own doc comment) -- up here with mapViewOpen because
// they are the same kind of flag: whoever owns the screen, the game
// itself is frozen, and resetToMenu/drawPlantPath both have to know.
static bool roomViewerOn;
static bool sweepOn;
static u16 mapBlinkTimer;
static u8 shakeFramesLeft;
// Squash-on-impact state (see triggerSquash further down). Up here beside
// the shake's own because loadRoom, which clears it, comes before both.
// Set when the cartography screen was opened by pressing A rather than
// by booting: its completion hands straight over to the run instead of
// going back to the menu.
static bool cartographyThenPlay;
static u8 squashFramesLeft;
static u8 squashDir;
// BUG FIX (user report: "al golpear el muro se queda permanente"). A ship
// held into a wall stays blocked (position unchanged) for as long as the
// player keeps it that way -- NORMAL/INERTIA/TOMB never reset p->dir on a
// stop, so every single sub-step re-detects the same collision and kept
// re-arming a fresh shake right as the previous one was about to finish,
// reading as permanent. This tracks whether the ship was ALREADY blocked
// on the previous check, shared by both places that trigger a wall shake
// (the normal-room sub-step loop and the insertion room's single call --
// never both in the same frame, one static flag covers either): only the
// FALSE -> TRUE transition triggers a new shake, matching "solo el primer
// contacto" -- staying jammed afterward doesn't keep restarting it.
static bool wasWallBlocked;
static u8 shakeAmplitude = 1; // set by triggerShake(), read by updateShake()
// How far (px) the ship has slid since its last stop -- persists across
// frames (a TOMB slide can span several), reset to 0 the moment a shake
// actually fires (see triggerShake), so it always measures THIS slide's
// run-up, never a previous one's.
static u16 slideDistance;

// Debug display (user request: "quiero que hagas un display debug",
// simplified further: "dame una combinacion facil que no interfiera con
// el juego"). A CHORD (all 3 held together), same shape as RESET_COMBO/
// -- B and C are never used on their own
// anywhere in gameplay (only inside these chords), and DOWN keeps it
// distinct from both of those (UP-based). Checked every frame regardless
// of gameState, so it works from the menu too.
#define DEBUG_VIEW_COMBO (BUTTON_B | BUTTON_C | BUTTON_DOWN)
static bool debugViewOn;

// "RUTAS" debug mode (named per user request, so it has a name to refer
// to it by) -- the slide-graph edges overlay: "haz un modo debug nuevo...
// que pinte lineas en todas las aristas navegables por la nave". First
// version used B+C+LEFT, same chord family as DEBUG_VIEW_COMBO/
// RESET_COMBO -- but LEFT/UP/DOWN are all live
// steering input during STATE_PLAYING, so that combo also nudged the
// ship while toggling (user report: "que no interfiera en el
// movimiento"). Fix: B+C alone, NO direction held at all -- the one
// combo in this family that can never double as steering, precisely
// because it doesn't use a direction. That makes it a literal subset of
// all three of the others (B+C+UP/DOWN and A+B+C+UP all contain plain
// B+C), so unlike them it can't be matched with a simple equality check:
// DEBUG_EDGES_EXCLUDE below must also be confirmed clear, or pressing
// e.g. DEBUG_VIEW_COMBO (B+C+DOWN) would fire this one too.
// Actual drawing lives in maze.c (Maze_setDebugEdgesVisible/
// Maze_drawDebugEdges) -- this combo just flips that flag and forces one
// redraw so it's visible immediately.
#define DEBUG_EDGES_COMBO (BUTTON_B | BUTTON_C)
#define DEBUG_EDGES_EXCLUDE (BUTTON_A | BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT | BUTTON_START)

// "HITBOX" debug mode (named per the same "give it a name" request RUTAS
// got) -- draws the two different collision shapes player.c/enemy.c
// actually test, user request: "pinte los gizmos de las colisiones".
// B+C+START: same B+C family as the other debug combos, but START (never
// used anywhere else in this game, not even a pause menu) instead of a
// direction -- same "no interferir en el movimiento" reasoning that moved
// RUTAS off B+C+LEFT, just solved by picking a button that was never
// steering input in the first place instead of dropping the direction
// entirely (B+C alone was already claimed by RUTAS). B+C+START is a
// strict SUPERSET of DEBUG_EDGES_COMBO (plain B+C), not a subset of
// anything else, so it needs no exclude mask of its own -- but
// DEBUG_EDGES_EXCLUDE above had to gain BUTTON_START, or holding this
// combo would also fire RUTAS every time.
#define DEBUG_HITBOX_COMBO (BUTTON_B | BUTTON_C | BUTTON_START)

// "DATOS" (user request: the FPS counter and the room's seeds should only
// show in debug). B+C+RIGHT: same B+C family as the rest, and RIGHT is
// the one direction none of the others claim -- VISOR has DOWN, RESET has
// UP, and RUTAS deliberately uses no direction at all. It is a superset
// of plain B+C, but DEBUG_EDGES_EXCLUDE already lists BUTTON_RIGHT, so
// holding this does not also fire RUTAS; and it is not a subset of
// RESET_COMBO (A+B+C+UP), which tests UP. Like B+C+DOWN it nudges the
// ship right while held, which is the same trade VISOR already makes.
// Off by default: both readouts are developer information.
#define DEBUG_INFO_COMBO (BUTTON_B | BUTTON_C | BUTTON_RIGHT)
static bool debugInfoOn;

// "PLANTA", the route to the nearest plant (user request: "haz un debug
// que pinte la trayectoria que hay que tomar a la planta mas cercana").
// No longer a debug chord: it is a feature of the game now, on a lone B
// during play (user request: "in-game B+START pasa a ser una feature del
// juego, se acciona con B"), HELD rather than toggled (user request: "B
// in game muestra el path con el botón mantenido") -- same rule as the
// map device on A, up while the button is down and gone the instant it
// is released.
//
// The exclude mask is what keeps a bare B a bare B: every debug chord
// that contains B also contains C or START (B+C, B+C+DOWN, B+C+START) or
// A (A+B+C+UP), and A held is the map screen, where B is its own fog
// toggle instead. Pressing B while any of those is down must not flip
// this.
#define PLANTPATH_BUTTON BUTTON_B
#define PLANTPATH_EXCLUDE (BUTTON_A | BUTTON_C | BUTTON_START)
static bool plantPathOn;
// The route only changes when the ship moves to a different cell, when a
// plant is taken, or on a room change -- so it is computed then and left
// alone otherwise, instead of re-running the search every frame. Declared
// up here because loadRoom(), further down, marks it dirty.
static s16 plantPathLastX = -1, plantPathLastY = -1;
static bool plantPathDirty = TRUE;
static bool hitboxDebugOn;

// Tile positions (BG_B, 8px units) the gizmo drew last frame -- cleared
// before drawing this frame's (player/enemy moved in between), instead of
// a full-plane clear every frame. 2 entities (player + enemy) * (4 AABB
// corners + 4 wall-probe corners) = 16, worst case.
#define HITBOX_MAX_MARKS 16
static u16 hitboxMarkX[HITBOX_MAX_MARKS];
static u16 hitboxMarkY[HITBOX_MAX_MARKS];
static u8 hitboxMarkCount;

// White-noise easter egg (user request: "crea una pantalla que sea ruido
// visual, white noise", folded into the map button per later requests:
// "la A es como un dispositivo mapa. Cuando el player la coge, pulsando A
// en lugar de ver ruido ya ve el mapa" -- letter A doubles as the in-
// fiction "map device", so BUTTON_A shows static instead of the real
// guide map until the player has actually collected letter A
// (Items_collectedCount()). Both halves are HELD, not toggled (user
// request, first for the static -- "el ruido es mientras esta pulsado A"
// -- then extended to the real map too -- "el mapa tampoco se activa, si
// no que se muestra mientras A esta pulsado"): whichever one is showing,
// it lasts for exactly as long as A stays down and vanishes the instant
// it's released, no on/off memory either way -- the static like fiddling
// with a TV with no signal, the map like holding a viewfinder up to your
// eye. TRUE for the entire time mapViewOpen is TRUE because of the static
// path rather than the real map -- checked back in that same per-frame
// block below to know which of the two mapViewOpen actually means right
// now.
static bool mapShowingNoise;
// What the static says (user request: "pinta un mensaje en la pantalla
// del noise de no hay senal de mapa"). No tilde and no enye: SGDK's own
// font is plain ASCII. BG_B, high priority, so the noise repainting all
// of BG_A every frame leaves it alone -- and cleared by hand on the way
// out, since nothing else ever writes that row.
// The "everything on this planet is collected, take it back" hint -- see
// drawReturnToInsertHint(), far below, for how it pulses. Declared up
// here because resetToMenu() has to wipe it on the way out.
#define RETURN_MSG_TEXT      "BACK TO SPACE"
#define RETURN_MSG_LEN       13
#define RETURN_MSG_ROW       3
#define RETURN_MSG_INK_INDEX ((PAL2 * 16) + 15)
#define RETURN_MSG_HOLD      6                       // frames per step of the ramp
#define RETURN_MSG_PULSE     (16 * RETURN_MSG_HOLD)  // 8 steps up, 8 down: ~1.6s
#define RETURN_MSG_PERIOD    600                     // one pulse every 10s at 60fps
static u16 returnMsgPhase;
static bool returnMsgShown;

#define NO_MAP_SIGNAL_TEXT "- SIN SENAL DE MAPA -"
#define NO_MAP_SIGNAL_LEN  21
#define NO_MAP_SIGNAL_ROW  13
#define NO_MAP_SIGNAL_BOX_X ((40 - (NO_MAP_SIGNAL_LEN + 2)) / 2)

// Rows on BG_B the debug panel prints to (user request: "toda la
// informacion que consideres necesaria... en tiempo real") -- left-
// aligned at column 1, same convention as every other BG_B text draw in
// this file, starting one row below INSERT_STATUS_ROW's own line so
// neither ever overlaps the other.
#define DEBUG_VIEW_FIRST_ROW 4
#define DEBUG_VIEW_ROWS      10

// moveSpeed (see the STATE_PLAYING block) is a local recomputed every frame
// -- this just mirrors its current value for drawDebugView(), which runs
// outside that scope.
static u8 moveSpeedDebug;
static GameState gameState;
static u8 sizePresetIndex = DEBUG_FIRST_PLANET ? DEBUG_FIRST_PLANET : SIZE_PRESET_DEFAULT;

static void invalidateHudLines(void); // defined with the HUD drawing below
static u16 plantCacheSlot(u8 planet, u8 col, u8 row); // defined with loadRoom below

// Planet progression (user request: "haz que cargue solo en el buffer los
// 3 primeros mundos. Los 3 grandes estan bloqueados, se desbloquean con
// el 60% del computo de mundos anteriores"). Only the first
// PLANETS_AT_START exist when the game boots -- which is also exactly how
// many the boot cartography maps, so nothing is computed for a planet
// nobody can reach yet. Each further one opens when the planets already
// available are PLANET_UNLOCK_PERCENT complete, counting plants AND
// letters pooled into a single ratio.
//
// Deliberately NOT persisted anywhere: presetSave lives in RAM for this
// power-on session only, same as every other bit of progress here.
#define DEBUG_UNLOCK_ALL 1 // temporal: pon 0 para recuperar la progresion normal
#if DEBUG_UNLOCK_ALL
#define PLANETS_AT_START       SIZE_PRESET_COUNT
#else
#define PLANETS_AT_START       3
#endif
#define PLANET_UNLOCK_PERCENT 60
static u8 unlockedPlanets = PLANETS_AT_START;

// Per-planet saved progress (spec §35, user request: "cada planeta tenga
// el recuento de sus letras obtenidas... si el player sale de un
// planeta, esas letras se conservan"). hasSave=FALSE for all until the
// ship actually leaves a planet mid-run, used to decide whether
// collectedCount/plantsCollected reflect real progress or read as 0.
// mapSeed is the piece that lets a resumed game regenerate the EXACT
// same map: GuideMap_generate() pulls from the shared random() stream,
// which (unlike each room's own layout, maze.c's Maze_generateRoom
// already calls setRandomSeed(roomSeed) for that) isn't itself reseeded
// from mapSeed automatically -- newGame() does that explicitly, so
// replaying the same mapSeed reproduces the same room tree/item
// placement/insertion link deterministically, same idea as maze.c
// already relies on at the per-room level. collectedCount is enough to
// restore item state too (Items_fastForward) since collection is always
// strictly in order (spec §13) -- never a subset.
// plantsCollected mirrors collectedCount for plants (spec §51,
// Plants_setCollected -- no ordering to replay, just the raw count).
// plantsTotal/plantsTotalKnown (spec §51, user request: "pintar en el
// menu un status de cuantas ha recogido el player en cada planeta",
// which needs a denominator) cache the full-map plant count
// scanPlanetPlantTotal() computes -- unlike collectedCount/
// plantsCollected, this isn't something the normal play loop produces
// for free: a planet's total is NOT a fixed preset constant
// (sizePresets[].letters is, plants are not), computing it means
// generating every room in the map to see how many plants spec §48's
// slide-graph placement puts in each one. newGame() computes it exactly
// once per planet, the first time it's actually entered each boot (spec
// §55 -- tried computing all 8 up front at boot instead, spec §52, but
// that needed random() seeded before the player had pressed anything,
// which spec §54 found means it always draws the same "random" numbers;
// reverted per user request, "prefiero que generes cada planeta al
// entrar").
// plantsMask (user report: "94 DE 88 PLANTAS" -- collected exceeding the
// real total): plantsCollected alone can't resume correctly, since unlike
// letters a plant has no enforced order, so a raw count can't say WHICH
// ones are already gone -- see plants.c's Plants_setCollected doc comment
// for the full story. This is the resumed snapshot of plants.c's own
// collectedMask (Plants_saveMask/restoreMask), taken alongside
// plantsCollected so a resumed planet remembers exactly which rooms are
// already cleared instead of re-offering (and re-counting) their plants.
typedef struct
{
    bool hasSave;
    u16 mapSeed;
    u8 collectedCount;
    u16 plantsCollected;
    u16 plantsMask[MAX_MAP_ROWS][MAX_MAP_COLS];
    // Which rooms' enemies are already dead (user request: a planet being
    // resumed keeps "sus enemigos ya desaparecidos"). Nothing else in this
    // struct implies it -- a killed enemy leaves no trace in the letter or
    // plant counters -- and GuideMap_generate() wipes the live bits every
    // time a planet is set up, so they have to travel in the save.
    bool enemyDead[MAX_MAP_ROWS][MAX_MAP_COLS];
    // Rooms already walked into, one bit per room (guidemap.h's
    // GuideMap_saveVisited). Without this the map came back blank after
    // dying, so a player returning to finish a planet lost every bit of
    // the ground they had already covered.
    u16 visitedMask[MAX_MAP_ROWS];
    u16 plantsTotal;
    bool plantsTotalKnown;
    // How many letters this planet's map COULD actually place. itemCount
    // starts at sizePresets[].letters but GuideMap_generate lowers it when
    // the map has fewer dead-end rooms than that, and with shaped maps
    // that happens for real. Completion compared against the preset
    // constant instead, so such a planet could never be finished and the
    // unlock percentage stalled with it. 0 = not generated yet.
    u8 lettersActual;
    // Score (user request): one running total per planet, not per visit --
    // leaving for the menu and coming back carries on where it left off,
    // so a planet cannot be farmed by restarting it.
    u32 scoreFrames;  // time spent playing this planet, in frames
    u16 scoreMoves;   // slides started (a press into a wall is not a move)
    u8  scoreRooms;   // rooms this planet's map really has, what the start value scales by
    bool scoreDone;   // objectives met: the clock has stopped
} PresetSave;
static PresetSave presetSave[SIZE_PRESET_COUNT];

// The letter target to judge planet i by: what its map really placed,
// falling back to the preset until that is known.
// ---------------------------------------------------------------------
// Score (user request: "empieza por una cifra grande y vete descontando
// puntos a medida que el player hace movimientos y transcurre el
// tiempo").
//
// The start value scales with the planet's own size because everything
// else does: measured over 40 generated maps per planet, a room carries
// ~11.5 plants and exactly 1 enemy, and rooms run from 1 to ~41. A flat
// start would make the small planets trivial to max and the big ones
// impossible, so the room is the unit.
//
// Two drains, deliberately the same size, so neither strategy dominates:
// rushing badly costs as much as thinking too long.
//   score = START*rooms - moves*MOVE - seconds*SECOND   (floored at 0)
//
// These three are the tuning knobs -- the numbers below are a starting
// point from the measured content, not from play, and want one real run
// to settle.
#define SCORE_PER_ROOM   2500 // planet 7 (~41 rooms) therefore starts at ~102000
#define SCORE_PER_MOVE     25
#define SCORE_PER_SECOND   25

static u32 scoreFor(u8 i)
{
    const u32 start = (u32) presetSave[i].scoreRooms * SCORE_PER_ROOM;
    const u32 spent = ((u32) presetSave[i].scoreMoves * SCORE_PER_MOVE) +
                      ((presetSave[i].scoreFrames / 60) * SCORE_PER_SECOND);

    return (spent >= start) ? 0 : (start - spent);
}

// Rooms this planet's map really has (after guidemap's own leaf pruning),
// and whether every one of their enemies is dead. The enemy count is one
// per room by construction, so the two walk the same grid.
static u8 countPlanetRooms(void)
{
    u8 col, row, n = 0;

    for (row = 0; row < mapRows; row++)
        for (col = 0; col < mapCols; col++)
            if (guideMap[row][col].type == CELL_ROOM)
                n++;

    return n;
}

static bool allEnemiesDead(void)
{
    u8 col, row;

    for (row = 0; row < mapRows; row++)
        for (col = 0; col < mapCols; col++)
            if ((guideMap[row][col].type == CELL_ROOM) && !GuideMap_isEnemyDead(col, row))
                return FALSE;

    return TRUE;
}

static u8 lettersFor(u8 i)
{
    return presetSave[i].lettersActual ? presetSave[i].lettersActual : sizePresets[i].letters;
}

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
// Which of the doorway's two cells the ship comes out on. It KEEPS the
// one it left the previous room by (user request) instead of being
// realigned onto the door's own offset cell every time: both rooms share
// that door's offset (GuideMap_doorOffset guarantees it), so the cell it
// was travelling along is a cell of this side's doorway too, and the ship
// no longer jumps sideways up to 16px crossing a door.
//
// The fallback matters: `along` is only a lane when it is cell-aligned
// AND inside this door's span. A cold start into the insertion room has
// no previous room to have come from, and player.x/y are whatever the
// last run left there.
static s16 doorLanePx(s16 along, u8 offset)
{
    const s16 tile = (s16) (along / MAZE_TILE_PX);

    if (((along % MAZE_TILE_PX) == 0) && ((tile == offset) || (tile == offset + 1)))
        return along;

    return (s16) (offset * MAZE_TILE_PX);
}

static void positionPlayerEnteringViaDoorDir(u8 doorDir, u8 offset)
{
    switch (doorDir)
    {
        case DOOR_N:
            player.y = MAZE_TILE_PX;
            player.x = doorLanePx(player.x, offset);
            break;
        case DOOR_S:
            player.y = MAZE_TILE_PX * (MAZE_H - 2);
            player.x = doorLanePx(player.x, offset);
            break;
        case DOOR_E:
            player.x = MAZE_TILE_PX * (MAZE_W - 2);
            player.y = doorLanePx(player.y, offset);
            break;
        default: // DOOR_W
            player.x = MAZE_TILE_PX;
            player.y = doorLanePx(player.y, offset);
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
    const u16 ox = Maze_originColumn(); // a maze cell is one VDP tile now (maze.h)
    const u16 oy = Maze_originRow();
    u16 tx, ty;

    switch (insertRoomDoorDir)
    {
        case DOOR_N:
            s[0] = '^';
            tx = (u16) (ox + insertLinkOffset);
            ty = (u16) (oy + 2);
            break;
        case DOOR_S:
            s[0] = 'v';
            tx = (u16) (ox + insertLinkOffset);
            ty = (u16) (oy + MAZE_H - 3);
            break;
        case DOOR_E:
            s[0] = '>';
            tx = (u16) (ox + MAZE_W - 3);
            ty = (u16) (oy + insertLinkOffset);
            break;
        default: // DOOR_W
            s[0] = '<';
            tx = (u16) (ox + 2);
            ty = (u16) (oy + insertLinkOffset);
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
// is actually complete -- that case goes straight back to the menu
// instead (see the exitDirForDoorDir(menuDoorDir) branch below), it
// never lingers here.
#define INSERT_STATUS_ROW 2
static void drawInsertRoomStatus(bool show)
{
    // The "AUN FALTAN LETRAS" message is gone (user request: "quita el
    // texto... no me gusta"). The line is still cleared, because other
    // screens write to it and this is what takes it back off.
    (void) show;
    VDP_clearTextLineBG(BG_B, INSERT_STATUS_ROW);
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

// One cache slot per grid room (maze.h) -- the room grid must fit in them.
_Static_assert(MAX_MAP_COLS * MAX_MAP_ROWS <= MAZE_INSERT_CACHE_SLOT, "MAZE_ROOM_CACHE_SLOTS too small for the map grid");
// One room-attempt-cache bank per planet (spec §52) -- maze.c doesn't
// know about presets (same reasoning MAX_MAP_COLS/ITEM_COUNT already
// document for similar cross-module constants elsewhere in this file).
_Static_assert(MAZE_MAP_SLOTS == SIZE_PRESET_COUNT, "MAZE_MAP_SLOTS must equal SIZE_PRESET_COUNT");

// Generates every room of presetSave[presetIndex]'s map (spec §51, user
// request: "contar todas las plantas de cada planeta") and sums each
// one's Plants_lastRoomCount(). Every active door is generated LOCKED
// (not unlocked) -- matching loadRoom()'s own canonical spawn baseline
// below, which every room's plants are now always placed against
// regardless of its actual, dynamic lock state (user request: "cierra
// todas las puertas... se abren... siempre que se hayan recogido las
// plantas y la letra"). A SEALED door's threshold cells can truncate a
// candidate plant line early (plants.c's own Maze_isWall recheck, spec
// §48) -- scanning against a DIFFERENT lock assumption than the real
// in-game spawn used would silently under/over-count against what's
// actually achievable (the exact "94 DE 88" class of bug already fixed
// once this session, spec §51's own PresetSave doc comment, just via a
// different mechanism this time). "Every door locked" is also the one
// fixed, canonical baseline now (not "whatever happens to be unlocked
// this visit"), so this total stays stable regardless of progress, same
// as the comment this replaces already wanted.
//
// Also where every room's generation-attempt cache gets populated (spec
// §52): Maze_setActiveMapSlot(presetIndex) first, so every
// Maze_generateRoom() call below lands in THIS planet's own bank
// (maze.h, one per planet so playing a different one in between doesn't
// wipe this one's work the way a single shared bank used to) -- the
// whole reason newGame() (spec §55: called from there now, once per
// planet the first time it's actually entered -- user request: "prefiero
// que generes cada planeta al entrar", reverting §52's "all 8 at boot"
// after that turned out to need an awkward "press a button" workaround
// for SGDK's random() seeding, spec §54) no longer needs to (re)generate
// a room from scratch the first time the player actually walks into it
// during the SAME visit: this already did, right before, and left the
// winning attempt code cached.
//
// Expensive regardless (one real tombo generation per room) -- called at
// most once per planet per boot (ensurePlantsTotalKnown() below guards it
// with plantsTotalKnown), never from the main per-frame loop.
//
// Clobbers mapCols/mapRows/itemCount/mapSeed/guideMap/insertLink*/the
// current room's own grid -- harmless since ensurePlantsTotalKnown() (the
// only caller) immediately reinitializes every one of them itself for
// the actual game right after this returns, whenever it's the one
// actually starting a run (its OWN cache bank survives that, by design
// -- see Maze_setActiveMapSlot's own doc comment); calling it from the
// menu (without starting a run at all) just leaves those globals
// clobbered until a real newGame() reinitializes them later -- harmless
// too, nothing reads them while gameState is STATE_MENU.
// Pooled progress over the planets already available: plants and letters
// counted together into one percentage (user request). A planet nobody
// has mapped yet has no totals to divide by, so it doesn't contribute --
// and it BLOCKS the next unlock until it does, which is the point: a
// planet that just opened would otherwise be free progress, since it adds
// nothing to the denominator while it stays uncharted, and every planet
// left would open at once. Going to look at it in the menu is enough to
// map it (that's what the cartography screen is for), so this never
// deadlocks.
static bool planetUnlockProgress(u16 *outPercent, u16 *outMissing)
{
    u16 got = 0, total = 0, needed;
    u8 i;

    for (i = 0; i < unlockedPlanets; i++)
    {
        if (!presetSave[i].plantsTotalKnown)
            return FALSE; // uncharted: no denominator, no answer

        got = (u16) (got + presetSave[i].plantsCollected + presetSave[i].collectedCount);
        total = (u16) (total + presetSave[i].plantsTotal + lettersFor(i));
    }

    *outPercent = total ? (u16) (((u32) got * 100) / total) : 0;

    // How many more plants-or-letters that percentage is short by, which
    // is what the menu tells the player when they look at a planet they
    // can't enter yet (user request) -- a count of things to go and pick
    // up says more than a percentage does.
    needed = (u16) (((u32) total * PLANET_UNLOCK_PERCENT + 99) / 100);
    *outMissing = (needed > got) ? (u16) (needed - got) : 0;

    return TRUE;
}

// Opens at most ONE planet per call -- the next one along, never a run of
// them: the planet that just opened is uncharted, so planetUnlockProgress
// above refuses to answer again until the player has at least been to the
// menu and looked at it. Cheap enough to call every menu frame.
static void updatePlanetUnlocks(void)
{
    if (unlockedPlanets >= SIZE_PRESET_COUNT)
        return;

    {
        u16 percent, missing;

        if (planetUnlockProgress(&percent, &missing) && (percent >= PLANET_UNLOCK_PERCENT))
            unlockedPlanets++;
    }
}

static u16 scanPlanetPlantTotal(u8 presetIndex)
{
    u16 total = 0;
    u8 col, row;

    mapCols = sizePresets[presetIndex].cols;
    mapRows = sizePresets[presetIndex].rows;
    itemCount = sizePresets[presetIndex].letters;
    mapSeed = presetSave[presetIndex].mapSeed; // roomSeedFor() reads this global directly
    setRandomSeed(mapSeed);
    // Before GuideMap_generate(): the door offsets it picks depend on
    // this planet's room footprint (Maze_doorOffsetRange), and every
    // room generated afterwards is carved at this size.
    Maze_setRoomSize(sizePresets[presetIndex].roomW, sizePresets[presetIndex].roomH,
                     sizePresets[presetIndex].variety);
    GuideMap_setShape(sizePresets[presetIndex].map, sizePresets[presetIndex].cols,
                      sizePresets[presetIndex].rows);
    GuideMap_generate();
    presetSave[presetIndex].lettersActual = itemCount; // generate may have lowered it
    Maze_setActiveMapSlot(presetIndex);
    Maze_clearRoomCache(); // fresh bank (boot time: always empty anyway) -- defensive, cheap

    for (row = 0; row < mapRows; row++)
    {
        for (col = 0; col < mapCols; col++)
        {
            const MapCell cell = guideMap[row][col];
            bool doorN, doorE, doorS, doorW;
            u8 doorOffsets[4];
            u16 seed;
            bool isInsertLinkRoom;

            if (cell.type != CELL_ROOM)
                continue;

            isInsertLinkRoom = (col == insertLinkCol) && (row == insertLinkRow);
            doorN = cell.doorN || (isInsertLinkRoom && (insertLinkDir == DOOR_N));
            doorE = cell.doorE || (isInsertLinkRoom && (insertLinkDir == DOOR_E));
            doorS = cell.doorS || (isInsertLinkRoom && (insertLinkDir == DOOR_S));
            doorW = cell.doorW || (isInsertLinkRoom && (insertLinkDir == DOOR_W));
            doorOffsets[DOOR_N] = doorOffsetFor(col, row, DOOR_N);
            doorOffsets[DOOR_E] = doorOffsetFor(col, row, DOOR_E);
            doorOffsets[DOOR_S] = doorOffsetFor(col, row, DOOR_S);
            doorOffsets[DOOR_W] = doorOffsetFor(col, row, DOOR_W);
            seed = roomSeedFor(col, row);

            Maze_generateRoom(doorN, doorE, doorS, doorW, doorN, doorE, doorS, doorW,
                               doorOffsets, seed, (u8) ((row * MAX_MAP_COLS) + col));
            Plants_spawnForRoom(seed);
            // Keep what this pass already computed instead of throwing it
            // away: that is what spares the room its first-visit flood.
            Plants_cacheCurrentRoom(plantCacheSlot(presetIndex, col, row));
            total += Plants_lastRoomCount();
        }
    }

    return total;
}

// Ensures presetSave[presetIndex] has a decided mapSeed and a known plant
// total, computing/committing them right now if it doesn't yet have both
// (user request: "en el menu, enseña el total de plantas tambien cuando
// la nave no ha entrado... quiero 0 de X plantas" -- letters already show
// this on sight, a fixed preset constant (sizePresets[].letters), but a
// planet's plant total isn't (spec §51): it depends on the actual
// generated map, so showing it before ever playing means DECIDING that
// planet's map right here instead of waiting for "empezar"). Marks
// hasSave TRUE so newGame() later reuses this exact mapSeed instead of
// drawing a different one -- keeps the menu's preview and the real game
// consistent instead of showing one total and playing a map with another.
//
// Safe to call random() here even for a never-played preset: spec §54's
// finding only blocks doing this before ANY real button press at all --
// both callers (LEFT/RIGHT menu navigation, and newGame() itself) only
// ever run after the player has already pressed something real.
//
// Expensive the first time for a given preset (one real tombo generation
// per room, scanPlanetPlantTotal() above) -- behind a loading message,
// a no-op on every call after the first for the same preset
// (plantsTotalKnown itself is the guard).
static void ensurePlantsTotalKnown(u8 presetIndex)
{
    if (presetSave[presetIndex].plantsTotalKnown)
        return;

    if (!presetSave[presetIndex].hasSave)
    {
        presetSave[presetIndex].mapSeed = random();
        presetSave[presetIndex].hasSave = TRUE;
    }

    VDP_drawText("CARGANDO...", 14, 13);
    presetSave[presetIndex].plantsTotal = scanPlanetPlantTotal(presetIndex);
    presetSave[presetIndex].plantsTotalKnown = TRUE;
    VDP_clearPlane(BG_A, TRUE);
}

// The cartography page lives inside a margin of a fifth of the screen on
// each side (user request: "la pagina de cartografiados es texto con
// margenes de un 20% de espacio a cada lado") -- 8 columns of the 40, so
// everything it draws, the flooding glyphs included, sits in the 24
// columns between.
#define INTRO_MARGIN_COLS 8
#define INTRO_TEXT_COLS   (40 - (2 * INTRO_MARGIN_COLS))
// Where a line of `len` characters starts to sit centred inside those 24
// columns. Anything longer than the band just starts at the margin.
static u16 introTextX(u16 len)
{
    return (u16) (INTRO_MARGIN_COLS + ((len < INTRO_TEXT_COLS) ? ((INTRO_TEXT_COLS - len) / 2) : 0));
}

// "PULSA UN BOTON" prompt (STATE_INTRO, user request -- see the enum's
// own doc comment). Drawn once from main()'s boot code, BG_A (nothing
// else is using it yet at this point in boot) -- the main loop's
// STATE_INTRO branch just watches for the first press, same "draw once,
// then poll per frame" shape drawMenu()/the menu's own LEFT/RIGHT
// handlers already use.
// Inside the same margins the cartography page uses (user request: "la
// primera pantalla del enlace subespacial tiene que tener margen
// tambien") -- which meant breaking the two long lines in two, since
// neither fits in the 24 columns that leaves.
static void drawIntroPrompt(void)
{
    VDP_clearPlane(BG_A, TRUE);
    VDP_drawText("OVNI-TERM MK.VII", introTextX(16), 10);
    VDP_drawText("----------------", introTextX(16), 11);
    VDP_drawText("ENLACE SUBESPACIAL", introTextX(18), 13);
    VDP_drawText("INACTIVO", introTextX(8), 14);
    VDP_drawText("PULSA UN BOTON PARA", introTextX(19), 17);
    VDP_drawText("ESTABLECER CONEXION", introTextX(19), 18);
}

// Invents a galaxy name from the shared random() stream -- 2 syllables
// plus a sector digit, e.g. "XENYN-4". Called from introBegin(), right
// after the player's first real button press of the session (same spec
// §54 safety the menu's own LEFT/RIGHT navigation already relies on:
// random() isn't properly seeded until SGDK's joypad driver sees a real
// press, and this is it).
static void pickGalaxyName(char *out)
{
    static const char *const pre[] = { "XEN", "VOR", "KRYL", "THAL", "NEBU", "QUAS", "ZOR", "HELI", "MYRA", "OBEX" };
    static const char *const suf[] = { "ON", "AR", "IS", "OS", "YN", "UX", "EX", "IA", "OTH", "ERA" };
    const u8 i = (u8) (random() % 10);
    const u8 j = (u8) (random() % 10);
    const u8 n = (u8) (1 + (random() % 9));

    sprintf(out, "%s%s-%d", pre[i], suf[j], n);
}

// Frame-chunked planet precompute, STATE_INTRO only (user request:
// "empieza a soltar numeros y caracteres raros que puedes sacar del
// proceso de generar los planetas... durante ese tiempo cargas todos los
// planetas", refined further -- "en la pantalla de carga tiene que salir
// datos cada frame no simplemente una linea": a first version called
// ensurePlantsTotalKnown() once per planet, which blocks for that
// planet's ENTIRE scan before returning, so the screen could only ever
// update 8 times total, not every frame. introBegin()/introTick() below
// instead process exactly ONE ROOM per call -- the same per-room body
// scanPlanetPlantTotal() already has, just resumable across frames
// instead of looped in one blocking call -- so main()'s STATE_INTRO
// dispatch can call introTick() once per frame and have fresh, real data
// land on screen every single frame while it runs.
//
// The real per-room data lands on a single fixed BG_A status line
// (overwritten each time a room finishes, not accumulated) -- the fun,
// ever-changing part is a SEPARATE flood effect (introFloodStep(), own
// section below), refined once more -- "quiero que la cartografia
// animada... suelte un caracter cada frame y se inunde la pantalla de
// caracteres cambiantes... tiene que ser entretenido divertido verlo" --
// which fills the rest of BG_B with constantly-changing glyphs, one new
// one per frame, independent of how fast the real per-room work happens
// to land.
static u8 introPlanetIndex;
// One past the last planet this cartography run maps. At boot it is
// PLANETS_AT_START (user request: "haz que cargue solo en el buffer los 3
// primeros mundos" -- the rest are locked, so mapping them would be work
// for places nobody can go yet). Later the SAME screen runs again for a
// single planet, the moment one that just unlocked is first looked at in
// the menu: introPlanetIndex is that planet and this is one past it.
static u8 introPlanetLimit = PLANETS_AT_START;
static u16 introRoomCursor; // flattened row*mapCols+col into introPlanetIndex's own grid
static u16 introPlanetTotal;
// FALSE while still waiting for the prompt's first press; TRUE from
// introBegin() until introTick() reports every planet done. main()'s
// STATE_INTRO dispatch is the only reader/writer.
static bool introGenerating;

static void introSetUpPlanet(u8 presetIndex)
{
    mapCols = sizePresets[presetIndex].cols;
    mapRows = sizePresets[presetIndex].rows;
    itemCount = sizePresets[presetIndex].letters;
    if (!presetSave[presetIndex].hasSave)
    {
        presetSave[presetIndex].mapSeed = random();
        presetSave[presetIndex].hasSave = TRUE;
    }
    mapSeed = presetSave[presetIndex].mapSeed;
    setRandomSeed(mapSeed);
    // Before GuideMap_generate(): the door offsets it picks depend on
    // this planet's room footprint (Maze_doorOffsetRange), and every
    // room generated afterwards is carved at this size.
    Maze_setRoomSize(sizePresets[presetIndex].roomW, sizePresets[presetIndex].roomH,
                     sizePresets[presetIndex].variety);
    GuideMap_setShape(sizePresets[presetIndex].map, sizePresets[presetIndex].cols,
                      sizePresets[presetIndex].rows);
    GuideMap_generate();
    presetSave[presetIndex].lettersActual = itemCount; // generate may have lowered it
    Maze_setActiveMapSlot(presetIndex);
    Maze_clearRoomCache();
    introRoomCursor = 0;
    introPlanetTotal = 0;
}

// Call once, right after the player's first press on the STATE_INTRO
// prompt. Picks the galaxy name, draws the (fixed) BG_A header, and sets
// up planet 0 for introTick() to start chewing through.
static void introFloodReset(void); // defined below, next to introFloodStep -- introBegin() needs it first

// showGalaxy: the "RUMBO: GALAXIA x" header belongs to the boot run, the
// one that charts the sky you are arriving at. Charting one more planet
// later is not a journey anywhere (user request: "cuando cargas un nuevo
// planeta, no indiques que viajas a esa galaxia, solo cartografiando"),
// so that line is left off and only the work itself is announced.
static void beginCartography(u8 firstPlanet, u8 limit, bool showGalaxy)
{
    char buf[40];
    char galaxy[16];
    int len;

    VDP_clearPlane(BG_A, TRUE);
    if (showGalaxy)
    {
        pickGalaxyName(galaxy);
        len = sprintf(buf, "GALAXIA %s", galaxy);
        VDP_drawText(buf, introTextX(len), 2);
    }
    VDP_drawText("CARTOGRAFIANDO...", introTextX(17), 4);

    introPlanetIndex = firstPlanet;
    introPlanetLimit = limit;
    introSetUpPlanet(firstPlanet);
    introFloodReset();
}

static void introBegin(void)
{
    u8 limit = (u8) (DEBUG_FIRST_PLANET + BOOT_CARTOGRAPHY_COUNT);

    if (limit > SIZE_PRESET_COUNT)
        limit = SIZE_PRESET_COUNT;

    beginCartography(DEBUG_FIRST_PLANET, limit, TRUE);
}

// Processes exactly one cell of introPlanetIndex's grid (a real room, or
// a quick skip for a non-room cell) and, when it was a real room,
// overwrites the ONE fixed BG_A status line (row 6) with that room's own
// real data -- not accumulated/scrolled, just the latest. Call once per
// frame while STATE_INTRO is still generating. Returns TRUE once every
// planet is fully done (presetSave[].plantsTotal/plantsTotalKnown
// committed for all of them).
static bool introTick(void)
{
    const u16 cellCount = (u16) (mapCols * mapRows);
    const u8 col = (u8) (introRoomCursor % mapCols);
    const u8 row = (u8) (introRoomCursor / mapCols);

    if (introRoomCursor >= cellCount)
    {
        presetSave[introPlanetIndex].plantsTotal = introPlanetTotal;
        presetSave[introPlanetIndex].plantsTotalKnown = TRUE;

        introPlanetIndex++;
        if (introPlanetIndex >= introPlanetLimit)
            return TRUE;

        introSetUpPlanet(introPlanetIndex);
        return FALSE;
    }

    {
        const MapCell cell = guideMap[row][col];

        if (cell.type == CELL_ROOM)
        {
            const bool isInsertLinkRoom = (col == insertLinkCol) && (row == insertLinkRow);
            const bool doorN = cell.doorN || (isInsertLinkRoom && (insertLinkDir == DOOR_N));
            const bool doorE = cell.doorE || (isInsertLinkRoom && (insertLinkDir == DOOR_E));
            const bool doorS = cell.doorS || (isInsertLinkRoom && (insertLinkDir == DOOR_S));
            const bool doorW = cell.doorW || (isInsertLinkRoom && (insertLinkDir == DOOR_W));
            const u8 doorOffsets[4] = {
                doorOffsetFor(col, row, DOOR_N), doorOffsetFor(col, row, DOOR_E),
                doorOffsetFor(col, row, DOOR_S), doorOffsetFor(col, row, DOOR_W),
            };
            const u16 seed = roomSeedFor(col, row);
            u16 roomPlants;
            char buf[40];
            int len;

            Maze_generateRoom(doorN, doorE, doorS, doorW, doorN, doorE, doorS, doorW,
                               doorOffsets, seed, (u8) ((row * MAX_MAP_COLS) + col));
            Plants_spawnForRoom(seed);
            Plants_cacheCurrentRoom(plantCacheSlot(introPlanetIndex, col, row)); // same reason as the scan above
            roomPlants = Plants_lastRoomCount();
            introPlanetTotal = (u16) (introPlanetTotal + roomPlants);

            // Trimmed to fit the margins: the same four numbers, shorter
            // labels. Room, seed, slide-graph nodes, plants.
            len = sprintf(buf, "R%02d,%02d S:%04X N:%02X P:%X",
                          col, row, seed, Maze_slideNodeCount(), roomPlants);
            VDP_drawText(buf, introTextX((u16) len), 6);
        }
    }

    introRoomCursor++;
    return FALSE;
}

// Flood effect (user request: "quiero que la cartografia animada del
// principio suelte un caracter cada frame y se inunde la pantalla de
// caracteres cambiantes... tiene que ser entretenido divertido verlo") --
// purely decorative, one glyph per frame at a sweeping cursor that wraps
// forever (so the screen stays busy for however long the real
// precompute above actually takes, independent of it). BG_B, below
// introTick()'s own BG_A status line so the two never overlap.
//
// Its own tiny xorshift PRNG, NOT the shared random()/setRandomSeed()
// stream introSetUpPlanet() above seeds per planet -- introTick() is
// interleaved with this every frame, and drawing from the SAME stream
// for cosmetic noise would desync the deterministic map/room generation
// from its own mapSeed (same reasoning plants.c's own independent PRNG
// doc comment gives). Fixed seed: this is pure decoration, nothing
// depends on it varying between boots.
#define INTRO_FLOOD_FIRST_ROW 8
#define INTRO_FLOOD_ROWS      (28 - INTRO_FLOOD_FIRST_ROW)
#define INTRO_FLOOD_CELLS     (INTRO_TEXT_COLS * INTRO_FLOOD_ROWS)

static u32 introFloodRng;
static u16 introFloodCursor;

static u32 introFloodNext(void)
{
    u32 x = introFloodRng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    introFloodRng = x;

    return x;
}

static void introFloodReset(void)
{
    introFloodRng = 2463534242u; // nonzero -- xorshift's one hard requirement
    introFloodCursor = 0;
    VDP_clearPlane(BG_B, TRUE);
    invalidateHudLines();
}

// Call once per frame while STATE_INTRO is generating, alongside
// introTick() -- independent of it, always exactly one glyph.
static void introFloodStep(void)
{
    static const char glyphs[] = "0123456789ABCDEF*#%&+-/.:;<>=~^";
    char s[2];
    const u16 x = (u16) (INTRO_MARGIN_COLS + (introFloodCursor % INTRO_TEXT_COLS));
    const u16 y = (u16) (INTRO_FLOOD_FIRST_ROW + (introFloodCursor / INTRO_TEXT_COLS));

    s[0] = glyphs[introFloodNext() % (sizeof(glyphs) - 1)];
    s[1] = '\0';
    VDP_drawTextBG(BG_B, s, x, y);

    introFloodCursor++;
    if (introFloodCursor >= INTRO_FLOOD_CELLS)
        introFloodCursor = 0;
}

// TRUE once THIS room's own letter (if it has one) and every one of its
// plants are collected -- user request: "las puertas se abren en cada
// habitación siempre que se hayan recogido las plantas y la letra de la
// misma". Vacuously TRUE for a room with neither (most rooms have no
// letter at all; a room with 0 plants and no letter is already "clear").
static bool roomFullyCleared(u8 col, u8 row)
{
    char letter;

    return !Items_uncollectedAt(col, row, &letter) && Plants_allCollectedInRoom(col, row);
}

// Per-room, per-direction "this doorway has been opened" latch (bit
// DOOR_N/E/S/W). Requirement: "una salida abierta sigue abierta durante
// la partida" -- door state is NOT recomputed from scratch on every
// entry, because that let a door the player had already walked through
// show up sealed again later (walk out south, come back in from the
// north, and the south exit was walled up again). Once a doorway opens
// it stays open for the rest of the run; cleared in newGame().
//
// Latched from both sides at once (latchDoorOpen below), so a doorway
// opened from one room is open from its neighbour too -- it is one
// physical opening, not two independent ones.
static u8 doorOpenBits[MAX_MAP_ROWS][MAX_MAP_COLS];

// The grid neighbour a door leads to, or FALSE if it has none -- the
// insertion link's extra door (spec §29, no grid cell on the far side)
// or a door at the edge of the map.
static bool doorNeighbor(u8 col, u8 row, u8 dir, u8 *outCol, u8 *outRow)
{
    s16 ncol = col, nrow = row;

    switch (dir)
    {
        case DOOR_N: nrow--; break;
        case DOOR_E: ncol++; break;
        case DOOR_S: nrow++; break;
        default:     ncol--; break; // DOOR_W
    }

    if ((ncol < 0) || (nrow < 0) || (ncol >= mapCols) || (nrow >= mapRows))
        return FALSE;

    *outCol = (u8) ncol;
    *outRow = (u8) nrow;
    return TRUE;
}

static bool doorIsLatchedOpen(u8 col, u8 row, u8 dir)
{
    return (doorOpenBits[row][col] & (u8) (1 << dir)) != 0;
}

static void latchDoorOpen(u8 col, u8 row, u8 dir)
{
    u8 ncol, nrow;

    doorOpenBits[row][col] |= (u8) (1 << dir);

    // Same opening seen from the other side (DOOR_N <-> DOOR_S, E <-> W).
    if (doorNeighbor(col, row, dir, &ncol, &nrow))
        doorOpenBits[nrow][ncol] |= (u8) (1 << ((dir + 2) & 3));
}

// Recomputes and re-applies this room's door-lock state, then redraws --
// user request (see roomFullyCleared's own doc comment): combines the
// EXISTING letter-order lock (spec §16, a door sealed because the
// NEIGHBOR it leads to is a not-yet-due letter's own dead end) with the
// NEW per-room clear requirement, AND, so a door opens only when BOTH
// are satisfied.
//
// The door the player walks in through is never an exception here: it is
// already latched open (doorOpenBits, latched from both sides the moment
// the ship crosses), so it reads as open from this room too. That also
// settles the earlier "la nave se queda atrapada en la colision de la
// puerta" report -- the cell the ship is standing in as it enters can
// never be the one this turns into wall.
//
// Safe (and cheap) to call on a room that's already been generated this
// visit, with the lock state now different from last time:
// Maze_generateRoom() is specifically designed to allow that (maze.h's
// own doc comment: "a currently-locked door can still unlock on a later
// visit without this room's interior ever being regenerated") -- the
// interior/slide-graph is unaffected by the locked flags at all, only
// the border-sealing pass at the end is, and the interior itself replays
// from this room's cache instead of searching again. Callers: loadRoom()
// below (the room's first generation this visit) and the two collection
// handlers in the main loop, each time roomFullyCleared() just turned
// TRUE for the room the player is standing in.
static void applyRoomDoorLocks(u8 col, u8 row)
{
    const MapCell cell = guideMap[row][col];
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
    // Locked branches (spec §16): a door that exists in the room graph but
    // leads only to letters not due yet gets sealed -- short-circuit skips
    // GuideMap_isRoomLocked when there's no door at all in that direction,
    // so out-of-range neighbor coords are never read. Gated on cell.doorX
    // (the RAW tree bit), never the insertLinkDir-merged doorX above --
    // the insertion link is never part of the lock graph.
    const bool orderLockedN = cell.doorN && GuideMap_isRoomLocked(col, row - 1);
    const bool orderLockedE = cell.doorE && GuideMap_isRoomLocked(col + 1, row);
    const bool orderLockedS = cell.doorS && GuideMap_isRoomLocked(col, row + 1);
    const bool orderLockedW = cell.doorW && GuideMap_isRoomLocked(col - 1, row);
    // A door is open when it has ALREADY been opened once (latched, see
    // doorOpenBits -- "una salida abierta sigue abierta") or when this
    // room is now clear of its own plants+letter and nothing in the
    // letter order still seals it ("recogiendo todas las plantas y letra
    // de una habitacion se desbloquean las salidas disponibles").
    // Everything else stays walled, and every door that comes out open
    // latches so it can never close again.
    const bool cleared = roomFullyCleared(col, row);
    const bool openN = doorN && (doorIsLatchedOpen(col, row, DOOR_N) || (cleared && !orderLockedN));
    const bool openE = doorE && (doorIsLatchedOpen(col, row, DOOR_E) || (cleared && !orderLockedE));
    const bool openS = doorS && (doorIsLatchedOpen(col, row, DOOR_S) || (cleared && !orderLockedS));
    const bool openW = doorW && (doorIsLatchedOpen(col, row, DOOR_W) || (cleared && !orderLockedW));
    const bool lockedN = !openN;
    const bool lockedE = !openE;
    const bool lockedS = !openS;
    const bool lockedW = !openW;

    if (openN) latchDoorOpen(col, row, DOOR_N);
    if (openE) latchDoorOpen(col, row, DOOR_E);
    if (openS) latchDoorOpen(col, row, DOOR_S);
    if (openW) latchDoorOpen(col, row, DOOR_W);

    Maze_generateRoom(doorN, doorE, doorS, doorW,
                       lockedN, lockedE, lockedS, lockedW, doorOffsets, roomSeedFor(col, row),
                       (u8) ((row * MAX_MAP_COLS) + col));
    Maze_draw();
    Items_drawInRoom(col, row);
    Plants_drawInRoom(col, row);
}

// Plant-cache slots (plants.h): one per real room, per planet, packed so
// that only cells the planet's map shape actually fills take space. The
// cache is filled during the cartography screen -- the pass that already
// generates every room and used to throw the plant lines away -- so by
// the time a room is walked into, its line is already known and the
// expensive part of a first visit (maze.c's computePlantSafe flood,
// measured at ~100% of what a first visit costs) never runs during play.
//
// Base is the running total of rooms in the planets before this one.
// Computed once at boot, from the same ASCII map table the generator
// uses, so the two can never disagree.
static u16 planetSlotBase[SIZE_PRESET_COUNT];
static u16 planetSlotTotal;

static void initPlantCacheSlots(void)
{
    u8 p;

    planetSlotTotal = 0;
    for (p = 0; p < SIZE_PRESET_COUNT; p++)
    {
        u8 col, row;

        planetSlotBase[p] = planetSlotTotal;
        for (row = 0; row < sizePresets[p].rows; row++)
            for (col = 0; col < sizePresets[p].cols; col++)
                if (sizePresets[p].map[row][col] == '#')
                    planetSlotTotal++;
    }
}

// PLANTS_CACHE_SLOTS if this cell is not a room (or the table outgrew the
// cache) -- plants.c treats that as "no slot" and falls back to spawning
// the line on the spot, which is exactly the old behaviour.
static u16 plantCacheSlot(u8 planet, u8 col, u8 row)
{
    const char *const *map = sizePresets[planet].map;
    u16 slot = planetSlotBase[planet];
    u8 c, r;

    if ((row >= sizePresets[planet].rows) || (col >= sizePresets[planet].cols) ||
        (map[row][col] != '#') || (planetSlotTotal > PLANTS_CACHE_SLOTS))
        return PLANTS_CACHE_SLOTS;

    for (r = 0; r < row; r++)
        for (c = 0; c < sizePresets[planet].cols; c++)
            if (map[r][c] == '#')
                slot++;
    for (c = 0; c < col; c++)
        if (map[row][c] == '#')
            slot++;

    return slot;
}

static void loadRoom(u8 col, u8 row)
{
    const u16 seed = roomSeedFor(col, row);
    const MapCell cell = guideMap[row][col];
    const bool isInsertLinkRoom = (col == insertLinkCol) && (row == insertLinkRow);
    const bool doorN = cell.doorN || (isInsertLinkRoom && (insertLinkDir == DOOR_N));
    const bool doorE = cell.doorE || (isInsertLinkRoom && (insertLinkDir == DOOR_E));
    const bool doorS = cell.doorS || (isInsertLinkRoom && (insertLinkDir == DOOR_S));
    const bool doorW = cell.doorW || (isInsertLinkRoom && (insertLinkDir == DOOR_W));
    const u8 doorOffsets[4] = {
        doorOffsetFor(col, row, DOOR_N),
        doorOffsetFor(col, row, DOOR_E),
        doorOffsetFor(col, row, DOOR_S),
        doorOffsetFor(col, row, DOOR_W),
    };

    // First pass: build the interior/slide-graph with every ACTIVE door
    // provisionally LOCKED (not open) -- so Plants_spawnForRoom() below
    // places plants against the grid shape this room will actually have
    // by default (sealed until cleared, user request), same as it always
    // used to spawn against whatever the FINAL lock state already was
    // under the old single-pass design.
    //
    // BUG FIX (host-tested: 1529 of 6000 generated rooms had at least one
    // plant end up entombed inside a sealed door's own threshold cells --
    // an unrecoverable soft-lock, since a plant walled off can never be
    // collected, so the room could never clear, so its doors could never
    // open). Root cause: plants.c's own spawn-time Maze_isWall() recheck
    // (its own doc comment: a locked door's threshold can turn a real
    // slide-graph cell into a wall AFTER the graph is built) only
    // protects against whatever's ALREADY sealed at the exact moment
    // Plants_spawnForRoom() runs. Spawning against every door open first
    // (the previous version of this comment) left every door's threshold
    // cells free to place plants, then applyRoomDoorLocks() right after
    // sealed them anyway. Locking every active door here instead, before
    // ever spawning plants, makes that recheck actually keep plants off
    // every cell any door's sealing could ever claim. Plants_spawnForRoom
    // is already proven stable under locked-door grids (plants.c's own
    // earlier doc comment -- host fuzzed across every door/lock
    // combination); this is just one more valid combination, now the
    // representative one instead of an edge case.
    //
    // applyRoomDoorLocks() right after rebuilds the real graph/plants
    // against the ACTUAL combined lock state and redraws -- replays this
    // same cached attempt, cheap, no new search (maze.h's own doc
    // comment: a locked door can unlock later without the interior ever
    // being regenerated).
    // Second and later visits take the room's plant lines straight out of
    // plants.c's cache (user request: crossing a door stalled for a few
    // frames). Both passes above exist only to feed Plants_spawnForRoom
    // the grid it has to place against -- with the lines already known
    // there is nothing to place, so the first pass goes away too and
    // applyRoomDoorLocks' own generation is the only one left. It replays
    // the same cached attempt either way (maze.h), so the grid it
    // produces is identical.
    if (!Plants_loadCachedRoom(plantCacheSlot(sizePresetIndex, col, row)))
    {
        Maze_generateRoom(doorN, doorE, doorS, doorW, doorN, doorE, doorS, doorW, doorOffsets, seed,
                           (u8) ((row * MAX_MAP_COLS) + col));
        Plants_spawnForRoom(seed); // spec §48 -- own line per room, same roomSeed as the layout itself
        Plants_cacheCurrentRoom(plantCacheSlot(sizePresetIndex, col, row)); // never again for this room, this session
    }

    applyRoomDoorLocks(col, row); // final generate + draw (Maze_draw/Items_drawInRoom/Plants_drawInRoom), see its own doc comment
    squashFramesLeft = 0; // a wall hit does not carry over into the next room
    plantPathDirty = TRUE; // different room, different plants: the cached route is meaningless now

    guideMap[row][col].visited = TRUE;

    // One enemy per room (user request), same seed the room's own maze
    // uses -- deterministic per room, same persistence philosophy as the
    // layout itself (re-entering always spawns the identical enemy, same
    // lane/position/direction). BUG FIX (user report: "los enemigos no
    // reaparecen, si los matas en una room no vuelven a aparecer en esa
    // room") -- a room whose enemy was already killed (GuideMap_markEnemyDead,
    // called on a successful melee) never spawns a fresh one again: just
    // mark it dead-on-arrival (alive/deathTimer both FALSE/0, so
    // Enemy_isGone is immediately TRUE) and keep the sprite hidden, rather
    // than calling Enemy_spawnForRoom at all.
    if (GuideMap_isEnemyDead(col, row))
    {
        enemy.alive = FALSE;
        enemy.deathTimer = 0;
        SPR_setVisibility(enemySprite, HIDDEN);
    }
    else
    {
        Enemy_spawnForRoom(&enemy, seed, doorN, doorE, doorS, doorW, doorOffsets);
        PAL_setColor(ENEMY_INK_INDEX, ENEMY_DANGEROUS_COLOR);
        SPR_setPosition(enemySprite, enemy.x + Maze_originPxX(), enemy.y + Maze_originPxY());
        // Not always alive: a room with nowhere legal to put an enemy (every
        // open cell on a door trajectory) gets none -- see Enemy_spawnForRoom.
        SPR_setVisibility(enemySprite, enemy.alive ? VISIBLE : HIDDEN);
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

    // The ship just physically crossed this doorway, so it is open for
    // good from both sides (doorOpenBits' own doc comment) -- including
    // the side the room being entered will be generated with, one line
    // below.
    latchDoorOpen(currentCol, currentRow, enterDoorDir);
    loadRoom(currentCol, currentRow);
    positionPlayerEnteringViaDoorDir(enterDoorDir, doorOffsetFor(currentCol, currentRow, enterDoorDir));
}

static void newGame(void)
{
    static PresetSave save; // ~254 bytes kept off the stack, see tomboTooEasy


    mapViewOpen = FALSE;

    // Decides (if not already decided -- e.g. by browsing this planet in
    // the menu first, user request: "en el menu, enseña el total de
    // plantas tambien cuando la nave no ha entrado") this planet's
    // mapSeed and plant total, and commits hasSave=TRUE either way -- see
    // ensurePlantsTotalKnown()'s own doc comment. By this point the
    // player has already pressed LEFT/RIGHT/A navigating the menu just to
    // reach "empezar", so SGDK's random() (tools.c) is already properly
    // reseeded from real elapsed time (spec §54's own finding), never the
    // fixed default it starts at -- safe even when this specific preset's
    // own seed hasn't been decided until right now.
    ensurePlantsTotalKnown(sizePresetIndex);
    save = presetSave[sizePresetIndex]; // read AFTER -- hasSave/mapSeed/plantsTotal are always decided by now

    mapSeed = save.mapSeed;

    // Reseed explicitly from mapSeed so GuideMap_generate()'s own
    // random() sequence becomes reproducible from mapSeed alone (see the
    // presetSave doc comment above) -- needed for a resume (or a planet
    // whose seed ensurePlantsTotalKnown() just decided above) to
    // regenerate the identical map every time.
    setRandomSeed(mapSeed);

    mapCols = sizePresets[sizePresetIndex].cols;
    mapRows = sizePresets[sizePresetIndex].rows;
    itemCount = sizePresets[sizePresetIndex].letters; // spec §33

    // Before GuideMap_generate(): the door offsets it picks depend on
    // this planet's room footprint (Maze_doorOffsetRange), and every
    // room generated afterwards is carved at this size.
    Maze_setRoomSize(sizePresets[sizePresetIndex].roomW, sizePresets[sizePresetIndex].roomH,
                     sizePresets[sizePresetIndex].variety);
    GuideMap_setShape(sizePresets[sizePresetIndex].map, sizePresets[sizePresetIndex].cols,
                      sizePresets[sizePresetIndex].rows);
    GuideMap_generate();
    presetSave[sizePresetIndex].lettersActual = itemCount; // generate may have lowered it
    if (presetSave[sizePresetIndex].scoreRooms == 0)
        presetSave[sizePresetIndex].scoreRooms = countPlanetRooms(); // what the start value scales by
    // Switches to THIS planet's own room-attempt-cache bank (spec §52) --
    // already fully populated by ensurePlantsTotalKnown()'s scan above
    // (first entry, or an earlier menu browse) or an earlier real entry
    // this session, so every room loadRoom() generates from here on
    // replays instantly instead of searching from scratch. Deliberately
    // NOT cleared: that cached work is exactly what eliminates the
    // per-room pause this spec is about.
    Maze_setActiveMapSlot(sizePresetIndex);
    tombMoving = FALSE;
    tombQueuedDir = DIR_NONE;
    Items_reset();
    Plants_reset(); // spec §48 -- running counter, now also saved/restored per planet (spec §51)
    memset(doorOpenBits, 0, sizeof(doorOpenBits)); // fresh run: no doorway has been opened yet
    Plants_setPlanetTotal(presetSave[sizePresetIndex].plantsTotal); // for Plants_drawHud's "n/TOTAL"
    // save.hasSave is unconditionally TRUE here now (ensurePlantsTotalKnown()
    // above guarantees it) -- for a genuinely fresh run this just restores
    // all-zero state, identical to what Items_reset()/Plants_reset() already
    // left, so there's no separate "fresh start" branch to skip it in anymore.
    Items_fastForward(save.collectedCount); // spec §35 -- restore prior progress on this planet
    Plants_setCollected(save.plantsCollected); // spec §51
    Plants_restoreMask(save.plantsMask); // bug fix, see PresetSave's own doc comment
    // After GuideMap_generate() above, which cleared every one of them.
    // Items_fastForward's restored count is also what makes the map device
    // work again on a resumed planet (user request: "si ya ha recogido la
    // A en ese planeta y vuelve a entrar, el mapa tiene que ser
    // accesible") -- holding A reads Items_collectedCount(), so a planet
    // whose letter A is already in hand shows the real overlay instead of
    // the static, death or no death.
    GuideMap_restoreEnemyDead(save.enemyDead);
    GuideMap_restoreVisited(save.visitedMask); // the map comes back as the player left it
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
    // guidemap.c's own static opposite() uses for
    // the same 4-direction pairing), so the insertion room's
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
    Plants_drawHud();
    drawInsertRoomStatus(TRUE); // spec §34 -- always true here, itemCount is always >= 1
    // BUG FIX (user report + screenshot: still looked wrong after routing
    // the corridor through the center -- "coloca el principio de la nave
    // en la entrada moviendose en una direccion"). Spawning at the room's
    // CENTER made the very first frame of a run depend on that exact cell
    // being on the path -- fragile, and the actual root cause of the
    // "ship embedded in a wall" bug fixed earlier. Spawning at a door
    // instead needs no such guarantee at all: it's the same "just inside
    // this door" cell doorPunch/punchBorderDoor always keeps open, the
    // exact spot every LATER re-entry into this room already uses
    // (positionPlayerEnteringViaDoorDir below). Facing inward and already
    // moving (not DIR_NONE) -- true for every control mode, not just
    // -- so the very first thing the player sees is the ship
    // sliding in from the door, not sitting still in the middle.
    //
    // BUG FIX (user report: "la nave ahora entra por la entrada al juego.
    // Es al reves. Tiene que entrar por la salida del juego") -- spawning
    // at insertRoomDoorDir (the mission door, which leads OUT to the grid)
    // had it backwards: a fresh run should appear at the door that leads
    // BACK to the menu (menuDoorDir, "la salida del juego") and cross the
    // room toward the mission door, not the other way around -- the ship
    // "launches" in from the menu side and heads out into the field.
    positionPlayerEnteringViaDoorDir(menuDoorDir, menuDoorOffset);
    switch (menuDoorDir)
    {
        case DOOR_N: player.dir = DIR_DOWN;  break;
        case DOOR_S: player.dir = DIR_UP;    break;
        case DOOR_E: player.dir = DIR_LEFT;  break;
        default:     player.dir = DIR_RIGHT; break; // DOOR_W
    }
    SPR_setPosition(playerSprite, player.x + Maze_originPxX(), player.y + Maze_originPxY()); // room pixels -> screen: HUD band plus the room's own centring (maze.h)
    SPR_setVisibility(playerSprite, VISIBLE);
    SPR_setVisibility(enemySprite, HIDDEN); // no enemy in the insertion room (spec §27)
}

// Solar-system start menu (spec §31): each planet is one of
// sizePresets[] (same order, innermost orbit = smallest map). Redrawn
// on every LEFT/RIGHT (like the old text-only menu was) since
// VDP_clearPlane wipes the title/hint text along with everything else on
// BG_A -- the sun and planets are sprites now (spec §32septies), so they
// don't need to be redrawn here at all, only the text.
static void planetCompletedFlags(bool *out);

// How long each screen holds before handing over to the other. A button
// in the table cuts its turn short and goes straight back (user request);
// nothing interrupts the menu's own turn, since every button there
// already means something else.
#define SCREEN_ALTERNATE_FRAMES (8 * 60)
static u16 screenAlternateTimer;

// The high-score table. One row per planet, best first.
//
// Sorted by score rather than listed in planet order because that is what
// makes it a scoreboard, and the scores ARE comparable across planets on
// purpose: the start value scales with each planet's room count (see
// SCORE_PER_ROOM), so a good run on a small planet can legitimately beat
// a sloppy one on a big planet. A planet never played has no start value
// yet and shows as a dash rather than a zero it never earned.
static void drawMenu(void);       // all three defined below
static void drawScoreTable(void);
static void flushSprites(void);

// Menu -> table. The planets, sun and cursor are sprites, so they have to
// be taken off the table AND flushed, or they stay on screen over it.
static void enterScoreTable(void)
{
    gameState = STATE_SCORES;
    screenAlternateTimer = 0;
    Menu_hideSprites();
    flushSprites();
    drawScoreTable();
}

// Table -> menu. Same restore the end of a run does, minus the fade: the
// two screens swap in place.
static void leaveScoreTable(void)
{
    bool completed[SIZE_PRESET_COUNT];

    gameState = STATE_MENU;
    screenAlternateTimer = 0;
    drawMenu();
    planetCompletedFlags(completed);
    Menu_fadeInSprites(sizePresetIndex, unlockedPlanets, completed);
}

static void drawScoreTable(void)
{
    u8 order[SIZE_PRESET_COUNT];
    char buf[40];
    u8 i, j;
    int len;

    VDP_clearPlane(BG_A, TRUE);
    VDP_setTextPalette(PAL0); // the menu leaves it here too, but this screen must not depend on that

    for (i = 0; i < SIZE_PRESET_COUNT; i++)
        order[i] = i;

    // Insertion sort, descending: 8 entries, so the simplest thing that
    // is obviously right beats anything cleverer.
    for (i = 1; i < SIZE_PRESET_COUNT; i++)
    {
        const u8 v = order[i];

        for (j = i; (j > 0) && (scoreFor(order[j - 1]) < scoreFor(v)); j--)
            order[j] = order[j - 1];
        order[j] = v;
    }

    len = sprintf(buf, "PUNTUACIONES");
    VDP_drawText(buf, (u16) ((40 - len) / 2), 4);

    for (i = 0; i < SIZE_PRESET_COUNT; i++)
    {
        const u8 p = order[i];

        // A planet with no rooms recorded has never been entered. The
        // marker is a character, not a colour, so it reads regardless of
        // how the palette lands.
        if (presetSave[p].scoreRooms == 0)
            len = sprintf(buf, "%d  %-6s        ---", i + 1, Menu_planetName(p));
        else
            len = sprintf(buf, "%d  %-6s %9lu %s", i + 1, Menu_planetName(p),
                          scoreFor(p), presetSave[p].scoreDone ? "*" : " ");

        VDP_drawText(buf, 8, (u16) (8 + (i * 2)));
    }

    VDP_drawText("* PLANETA COMPLETADO", 8, 25);
    VDP_drawText("PULSA UN BOTON", 13, 27);
}

static void drawMenu(void)
{
    char buf[40]; // a full screen line: the longest sprintf here is the locked planet's "RECOGE n OBJETOS MAS PARA ABRIRLO"
    int len;
    const u8 letters = lettersFor(sizePresetIndex);
    // Saved progress for the selected planet (spec §35) -- 0 only when
    // it has never been played at all. A finished planet's save keeps
    // collectedCount == letters (spec §38 fix), so it correctly reads as
    // fully complete here instead of resetting back to 0.
    const u8 collected = presetSave[sizePresetIndex].hasSave ? presetSave[sizePresetIndex].collectedCount : 0;

    VDP_clearPlane(BG_A, TRUE);

    // The big OVNI, top right (user request: "desaparece el texto que
    // indica el switch del sonido. solo OVNI en su lugar, alineado a la
    // derecha"). It replaces both the sound line that used to be on row 1
    // and the small centred title that was on row 3 -- C still toggles the
    // sound, it just isn't announced any more. Drawn as tiles by menu.c,
    // 16x16 per letter, so it has to come after the VDP_clearPlane above.
    Menu_drawTitle();

    {
        bool completed[SIZE_PRESET_COUNT];
        planetCompletedFlags(completed);
        Menu_drawPlanetBar(sizePresetIndex, unlockedPlanets, completed);
    }

    // 3-line status block (user request: "n de total plantas \n n de
    // total letras \n pulsa a empezar") -- plants, then letters, same
    // "collected DE total X" wording for both, then the prompt.
    //
    // Plant status (spec §51, user request: "contar todas las plantas de
    // cada planeta... pintar en el menu un status de cuantas ha recogido
    // el player"). plantsTotalKnown is set by ensurePlantsTotalKnown(),
    // called both from here (LEFT/RIGHT navigation, just below) and from
    // newGame() -- so by the time this planet has ever actually been the
    // SELECTED one in the menu, not just played, this is already known
    // (user request: "enseña el total de plantas tambien cuando la nave
    // no ha entrado"). Still guarded rather than assumed unconditionally
    // true: the very first preset shown at cold boot, before any
    // LEFT/RIGHT/A has been pressed at all, hasn't had the chance yet
    // (spec §54's random()-before-any-button-press trap) -- skipped
    // entirely then rather than showing a misleading "0 DE 0".
    // A planet still locked says what it costs instead of what's in it
    // (user request: "planetas bloqueados son seleccionables, pero te
    // indica que objetos necesitan ser [recogidos] para entrar"). It has
    // no numbers of its own to show anyway -- a locked planet is never
    // charted, precisely because nobody can go there yet.
    if (sizePresetIndex >= unlockedPlanets)
    {
        u16 percent, missing;

        VDP_drawText("- BLOQUEADO -", (40 - 13) / 2, 25);

        if (planetUnlockProgress(&percent, &missing))
            len = sprintf(buf, "RECOGE %d OBJETOS MAS PARA ABRIRLO", missing);
        else
            len = sprintf(buf, "VISITA LOS PLANETAS YA ABIERTOS");

        VDP_drawText(buf, (40 - len) / 2, 26);
    }
    else
    {
    if (presetSave[sizePresetIndex].plantsTotalKnown)
    {
        len = sprintf(buf, "%d DE %d PLANTAS", presetSave[sizePresetIndex].plantsCollected,
                      presetSave[sizePresetIndex].plantsTotal);
        VDP_drawText(buf, (40 - len) / 2, 25);
    }

    // Letters (spec §33/§35): collected/faltan del planeta seleccionado --
    // progress is per-planet and survives leaving mid-run (see
    // presetSave), so this reflects that saved state, not the live game.
    // Replaces the old 2-line "N LETRAS" + "N DE N RECOGIDAS" pair with
    // one line matching the plants line's own "collected DE total" shape.
    len = sprintf(buf, "%d DE %d LETRAS", collected, letters);
    VDP_drawText(buf, (40 - len) / 2, 26);

    VDP_drawText("PULSA A PARA EMPEZAR", 10, 27);
    }

    // How close the next planet is to opening (user request: the rest of
    // them unlock at PLANET_UNLOCK_PERCENT of what's already available,
    // plants and letters pooled). Row 24 is the one free line between the
    // orbits and the status block. Nothing is drawn once they are all
    // open -- there is nothing left to wait for.
    if (unlockedPlanets < SIZE_PRESET_COUNT)
    {
        u16 percent, missing;

        if (planetUnlockProgress(&percent, &missing))
            len = sprintf(buf, "PROXIMO PLANETA: %d%% DE %d%%", percent, PLANET_UNLOCK_PERCENT);
        else
            len = sprintf(buf, "CARTOGRAFIA INCOMPLETA");

        VDP_drawText(buf, (40 - len) / 2, 24);
    }
}

// Hard reset combo (user request): A+B+C+UP together, from anywhere
// (menu or mid-game), drops back to the size-select menu. Checked ahead
// of the per-state input handling below so it always takes priority over
// whatever any of those 4 buttons would otherwise do that same frame.
#define RESET_COMBO (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_UP)

// Screen-to-screen fades (user request: "quiero fundido de negro en la
// pantalla de menu, y cuando empieza el juego", then refined -- "el
// fundido tiene que ser mas suave y permanecer en negro un cuarto de
// segundo. El fundido es al color 0 de la paleta, ese gris").
//
// It fades to the backdrop, not to black: colour 0 of palette 0 is the
// colour everything in this game sits on (maze.h's MAZE_BG_COLOR), so a
// screen faded to it reads as the room emptying out rather than as the
// console being switched off. Blocking on purpose -- SGDK runs its own
// vblank loop inside, and nothing should be moving across a screen
// change anyway.
#define FADE_FRAMES 24 // each way, 0.4s
#define FADE_HOLD   15 // a quarter second sitting on the backdrop

static u16 fadePal[64];
static u16 fadeFlat;

// The colour everything fades to and from: palette entry 0, which is the
// backdrop the VDP shows wherever nothing is drawn -- the menu's own
// background, and the floor of every room (user request: "fundidos tienen
// que ser al color de bg del menu, creo que es 0 en la paleta"). Read out
// of CRAM rather than written down here, so it stays the backdrop even if
// that colour ever changes.
static u16 fadeBackdrop(void)
{
    u16 c;

    PAL_getColors(0, &c, 1);

    return c;
}

// Pushes the sprite table to the VDP NOW instead of waiting for the main
// loop's own SPR_update at the end of the frame. Needed in the middle of
// a transition: SPR_setVisibility only marks a sprite, and until the
// table is actually sent the VDP keeps drawing whatever was in it -- for
// however long the setup after it takes, which is not a frame but a whole
// room generation. A sprite left there picks up every palette change that
// setup makes, and lights up over a screen that is supposed to be flat.
static void flushSprites(void)
{
    SPR_update();
    SYS_doVBlankProcess();
}

static void fadeToBackdrop(void)
{
    u16 flat[64];
    u16 i;

    // The sprite list the VDP is showing is whatever SPR_update last sent
    // it -- and these fades run their own vblank loop, outside the one in
    // the main loop that calls it. Without this, everything the caller
    // just hid or showed only reaches the screen when the frame after the
    // fade ends: the menu's planets stayed up, in full colour, over the
    // insertion room for the whole fade in (user report: "se cuelan los
    // planetas superpuestos").
    SPR_update();

    fadeFlat = fadeBackdrop();
    PAL_getColors(0, fadePal, 64);
    for (i = 0; i < 64; i++)
        flat[i] = fadeFlat;

    PAL_fadeTo(0, 63, flat, FADE_FRAMES, FALSE);

    for (i = 0; i < FADE_HOLD; i++)
        SYS_doVBlankProcess();
}

// Back up from the backdrop. Whatever the new screen set up while it was
// flat is in CRAM already -- but only the handful of slots it actually
// touched; every other one still holds the fade's own colour. So the
// target is "the new colour where there is one, the old one otherwise",
// and a slot counts as new exactly when it differs from the backdrop.
//
// One slot genuinely collides: Menu_setVisible(FALSE) sets PAL3's index0
// to that same backdrop grey (it hands that slot to maze.c's locked-door
// tiles on the way out), so this can't tell it apart from an untouched
// one. The menu->game path calls Menu_setVisible(FALSE) once more after
// fading back in, which is idempotent and settles it.
static void fadeFromBackdrop(void)
{
    u16 target[64];
    u16 flat[64];
    u16 i;

    SPR_update(); // same reason as above, and this is the half that showed

    PAL_getColors(0, target, 64);
    for (i = 0; i < 64; i++)
    {
        if (target[i] == fadeFlat)
            target[i] = fadePal[i];
        flat[i] = fadeFlat;
    }

    // Two reasons to flatten CRAM again before fading. One: whatever the
    // setup poked while the screen was down is sitting in there at full
    // strength -- it was written straight to CRAM, so it is on screen from
    // the instant it was written. Two: PAL_fadeInAll, which this used to
    // call, fades from BLACK rather than from wherever the palette is, so
    // every fade in began by snapping the whole screen to black. That snap
    // was the flicker. PAL_fadeTo starts from the current palette, which
    // is now exactly the flat screen the fade out left.
    PAL_setColors(0, flat, 64, CPU);
    PAL_fadeTo(0, 63, target, FADE_FRAMES, FALSE);
}

// Which planets show as completed (spec §45) -- every letter of theirs
// collected. Derived fresh from presetSave rather than tracked, which is
// cheap at SIZE_PRESET_COUNT checks and never goes stale. Both the menu's
// own per-frame update and the sprite fade-in need it.
static void planetCompletedFlags(bool *out)
{
    u8 i;

    for (i = 0; i < SIZE_PRESET_COUNT; i++)
        out[i] = presetSave[i].hasSave && (presetSave[i].collectedCount >= lettersFor(i));
}

static void resetToMenu(void)
{
    const GameState previousState = gameState; // capture before overwriting below

    fadeToBackdrop(); // the menu is built below, behind the black

    // Snapshot progress for the planet just left (spec §35, fixed in
    // §38: a finished planet used to clear its save entirely, which
    // showed as "0 DE N" in the menu right after completing it --
    // user-reported bug, should read as fully complete instead). Only
    // when actually leaving a real game (mid-run via the reset combo, or
    // just won -- completing the phase calls this directly from
    // STATE_PLAYING now, no separate win state in between), never when
    // the combo is pressed while already at the menu (mapSeed/items.c's
    // collected state would be stale leftovers from whatever was last
    // played, not "this" run). collectedCount naturally equals itemCount
    // when the run was won, which is exactly what should show in the menu.
    // Dying counts exactly the same as walking out (user request: "incluso
    // si la nave muere y es game over... quiero que guardes el estado del
    // planeta"). It used to be deliberately left out, so a death threw the
    // whole attempt away -- letters, plants and the map device with them.
    if ((previousState == STATE_PLAYING) || (previousState == STATE_GAMEOVER))
    {
        presetSave[sizePresetIndex].hasSave = TRUE;
        presetSave[sizePresetIndex].mapSeed = mapSeed;
        presetSave[sizePresetIndex].collectedCount = Items_collectedCount();
        presetSave[sizePresetIndex].plantsCollected = Plants_collectedCount(); // spec §51
        Plants_saveMask(presetSave[sizePresetIndex].plantsMask); // bug fix, see PresetSave's own doc comment
        GuideMap_saveEnemyDead(presetSave[sizePresetIndex].enemyDead);
        GuideMap_saveVisited(presetSave[sizePresetIndex].visitedMask);
    }

    updatePlanetUnlocks(); // this run's progress may have opened the next planet

    gameState = STATE_MENU;
    mapViewOpen = FALSE;
    cartographyThenPlay = FALSE; // a reset mid-cartography goes to the menu, not into a run
    roomViewerOn = FALSE; // RESET_COMBO can fire from inside either map-screen tool
    sweepOn = FALSE;
    inInsertRoom = FALSE; // harmless either way -- newGame() sets it back to TRUE when a new run starts

    SPR_setVisibility(playerSprite, HIDDEN);
    SPR_setVisibility(mapShipSprite, HIDDEN);
    SPR_setVisibility(enemySprite, HIDDEN);

    // Items_drawHud's letter tracker and drawInsertRoomStatus's message
    // (both BG_B, high priority) are only ever refreshed during gameplay
    // -- clear them so a reset mid-game doesn't leave either lingering
    // over the menu (BG_A's own VDP_clearPlane in drawMenu() below only
    // touches BG_A, a separate plane).
    VDP_clearTextLineBG(BG_B, 0); // drawRoomIdHud's line, and the FPS counter shares it
    invalidateHudLines();
    VDP_clearTextLineBG(BG_B, 1);
    VDP_clearTextLineBG(BG_B, INSERT_STATUS_ROW);
    VDP_clearTextLineBG(BG_B, RETURN_MSG_ROW); // the "go back" hint, same deal
    returnMsgShown = FALSE;

    // Same ordering rule in reverse: the ship and the enemy are off above,
    // but until the table is sent they are still being drawn -- and
    // Menu_setVisible(TRUE) is about to point their palette slots at the
    // sun's white and the completed planets' yellow.
    flushSprites();

    Menu_setVisible(TRUE);
    Menu_hideSprites(); // they come up by themselves, after the screen does
    drawMenu();
    fadeFromBackdrop();
    {
        bool completed[SIZE_PRESET_COUNT];

        planetCompletedFlags(completed);
        Menu_fadeInSprites(sizePresetIndex, unlockedPlanets, completed);
    }
}

// Starts a shake burst sized by `distance` (px the ship had slid before
// this impact -- see slideDistance's own doc comment), unless one is
// already playing (never restarts/extends an in-progress shake). Call
// whenever the ship hits a wall or an enemy this frame.
// Squash-on-impact (user request: "cuando golpea un muro se deforma...
// el grafico tiene que ser el protagonista estrujado contra la pared").
// playerShip carries 5 frames: 0 is the ship, 1..4 are it flattened
// against a wall, ordered to match player.h's DIR_UP/LEFT/DOWN/RIGHT --
// so the frame is simply 1 + the direction it was travelling when it
// stopped, which is by definition the wall it hit head-on.
//
// Held for as long as the screenshake the same impact triggers, so the
// two read as one event.
#define SQUASH_FRAMES SHAKE_DURATION_FRAMES

static void triggerSquash(u8 dir)
{
    if (dir == DIR_NONE)
        return;

    squashDir = dir;
    squashFramesLeft = SQUASH_FRAMES;
}

// Pushes the current frame to the sprite, but only when it actually
// changes: SPR_setFrame re-uploads the frame's tiles, so calling it every
// frame would be a VRAM transfer per frame for a sprite that is usually
// not deforming at all.
static void updateSquash(void)
{
    static u8 shown = 0xFF;
    u8 want;

    if (squashFramesLeft > 0)
    {
        squashFramesLeft--;
        want = (u8) (1 + squashDir);
    }
    else
        want = 0;

    if (want != shown)
    {
        shown = want;
        SPR_setFrame(playerSprite, want);
    }
}

static void triggerShake(u16 distance)
{
    if (shakeFramesLeft == 0)
    {
        u16 amp = 1 + (distance / SHAKE_DIST_PER_LEVEL);

        if (amp > SHAKE_MAX_AMPLITUDE)
            amp = SHAKE_MAX_AMPLITUDE;
        shakeAmplitude = (u8) amp;
        shakeFramesLeft = SHAKE_DURATION_FRAMES;
    }
}

// Applies (or clears) the current shake offset to BG_A. Call exactly once
// per frame, unconditionally regardless of gameState -- so a shake that's
// still counting down when e.g. a game over or win screen interrupts it
// still winds all the way back down to (0,0) instead of leaving BG_A
// stuck offset on a screen that never triggers it again.
static void updateShake(void)
{
    if (shakeFramesLeft > 0)
    {
        // Alternating on/off each frame -- never both axes at once (bit 1
        // offsets which parity Y uses), so it's never a diagonal jump,
        // just shakeAmplitude px nudging one way then the other on one
        // axis at a time.
        const s16 sx = (shakeFramesLeft & 1) ? shakeAmplitude : 0;
        const s16 sy = ((shakeFramesLeft & 3) == 2) ? shakeAmplitude : 0;

        shakeFramesLeft--;
        VDP_setHorizontalScroll(BG_A, sx);
        VDP_setVerticalScroll(BG_A, sy);
    }
    else
    {
        VDP_setHorizontalScroll(BG_A, 0);
        VDP_setVerticalScroll(BG_A, 0);
    }
}

// HITBOX gizmo helpers (see DEBUG_HITBOX_COMBO's own doc comment above).
// Draws onto BG_B (same trick as the HUD/debug panel: high priority, so
// it overlays BG_A's maze without touching it) one glyph per tile, and
// remembers each one so next frame's redraw can erase exactly those
// cells first -- a full-plane clear every frame would also work but is
// wasteful when the player/enemy only move a few pixels between frames.
static void clearHitboxMarks(void)
{
    u8 i;

    // One space glyph per marked tile, NOT VDP_clearTextLineBG -- that
    // clears the WHOLE row, and a hitbox mark can legitimately land on
    // row 0/1 (the FPS counter and Items_drawHud/Plants_drawHud's own
    // row) whenever the player/enemy is near the top of the room, which
    // would otherwise wipe those every time this redraws.
    for (i = 0; i < hitboxMarkCount; i++)
        VDP_drawTextBG(BG_B, " ", hitboxMarkX[i], hitboxMarkY[i]);
    hitboxMarkCount = 0;
}

static void markHitboxTile(s16 px, s16 py, const char *glyph)
{
    const u16 tx = (u16) ((px + Maze_originPxX()) >> 3); // 8px-per-VDP-tile, same unit VDP_drawTextBG's x/y already are
    const u16 ty = (u16) ((py + Maze_originPxY()) >> 3); // room pixels -> screen rows (maze.h)

    // The room starts below the HUD's own band now, so a mark can no
    // longer land on one of its two rows at all -- this only still guards
    // against a caller passing a position from outside the room.
    if (ty < MAZE_ORIGIN_ROW)
        return;

    if (hitboxMarkCount < HITBOX_MAX_MARKS)
    {
        VDP_setTextPriority(1);
        VDP_drawTextBG(BG_B, glyph, tx, ty);
        VDP_setTextPriority(0);
        hitboxMarkX[hitboxMarkCount] = tx;
        hitboxMarkY[hitboxMarkCount] = ty;
        hitboxMarkCount++;
    }
}

// Both collision shapes player.c/enemy.c actually test for an entity at
// pixel (x,y), top-left of its 16x16 box (Player.x/Enemy.x's own
// convention):
//   '+' -- the 4 corners of the full 16x16 box an overlap test uses. The
//          SHIP's side of every pickup is this box; what it is tested
//          against is smaller for a plant or a letter, which are one 8x8
//          glyph in the top-left quarter of their cell (plants.c/items.c).
//   'O'/'X' -- the 4 corners of the box wall-sliding probes against
//          Maze_isWall() (MAZE_TILE_PX - 1, player.c/enemy.c's own BOX --
//          not exported, so re-derived here by formula rather than
//          shared) -- 'X' if that exact corner currently reads as a wall
//          (why a slide in that direction would be blocked right now),
//          'O' if open. It used to be 2px smaller than the sprite, which
//          is what let a stop land a pixel past the cell grid; it is the
//          sprite's own last pixel now, so the two boxes coincide.
static void drawEntityHitbox(s16 x, s16 y)
{
    #define HITBOX_PROBE_BOX (MAZE_TILE_PX - 1) // mirrors player.c/enemy.c's own BOX

    markHitboxTile(x, y, "+");
    markHitboxTile((s16) (x + MAZE_TILE_PX - 1), y, "+");
    markHitboxTile(x, (s16) (y + MAZE_TILE_PX - 1), "+");
    markHitboxTile((s16) (x + MAZE_TILE_PX - 1), (s16) (y + MAZE_TILE_PX - 1), "+");

    {
        static const s16 dx[4] = { 0, HITBOX_PROBE_BOX, 0, HITBOX_PROBE_BOX };
        static const s16 dy[4] = { 0, 0, HITBOX_PROBE_BOX, HITBOX_PROBE_BOX };
        u8 i;

        for (i = 0; i < 4; i++)
        {
            const s16 px = (s16) (x + dx[i]), py = (s16) (y + dy[i]);
            const bool wall = Maze_isWall((s16) (px / MAZE_TILE_PX), (s16) (py / MAZE_TILE_PX));

            markHitboxTile(px, py, wall ? "X" : "O");
        }
    }

    #undef HITBOX_PROBE_BOX
}

// Call once per frame during STATE_PLAYING while hitboxDebugOn (no-op
// otherwise -- see updateDebugCombo's own toggle, which clears any
// leftover marks the moment it's turned off instead of waiting for this).
static void drawCollisionGizmos(void)
{
    if (!hitboxDebugOn || (gameState != STATE_PLAYING) || roomViewerOn || sweepOn)
        return;

    clearHitboxMarks();
    drawEntityHitbox(player.x, player.y);
    if (enemy.alive)
        drawEntityHitbox(enemy.x, enemy.y);
}

// "PLANTA", the route to the nearest plant (see PLANTPATH_BUTTON's own
// doc comment -- a feature of the game, on a lone B). Breadth-first
// search over SLIDES -- each step is a full slide until a wall, which is
// the only move the ship actually has -- from the cell the ship is on
// right now, stopping at the first slide that CROSSES something still to
// collect (see plantPathTargetAt). The winning route is then drawn cell
// by cell, each one an arrow showing which way to push there.
//
// Searching the real grid with Maze_isWall rather than the slide graph:
// the graph is seeded from the room's doors and still misses a couple of
// resting cells per room, and this needs to start from wherever the ship
// happens to be, which is frequently not one of them.
#define PLANTPATH_MAX_MARKS 96

static u16 plantPathMarkX[PLANTPATH_MAX_MARKS];
static u16 plantPathMarkY[PLANTPATH_MAX_MARKS];
static u8 plantPathMarkCount;

static s8 ppFromDir[MAZE_MAX_H][MAZE_MAX_W]; // which way the ship arrived here, -1 = not reached
static u8 ppFromX[MAZE_MAX_H][MAZE_MAX_W], ppFromY[MAZE_MAX_H][MAZE_MAX_W];
static u8 ppQX[MAZE_MAX_W * MAZE_MAX_H], ppQY[MAZE_MAX_W * MAZE_MAX_H];
static bool ppCrossed[MAZE_MAX_H][MAZE_MAX_W]; // reachMode only: every cell some slide crosses


static const s8 ppDX[4] = { 0, 1, 0, -1 }; // N,E,S,W -- maze.h's own order
static const s8 ppDY[4] = { -1, 0, 1, 0 };

// The ship does NOT stop against the border where a door is: reaching the
// border cell at a live door's 2-cell span while moving that way is an
// EXIT, the room changes under the player (user: "esta tomando el borde
// de la pantalla como una colision, pero eso es una salida y rompe el
// juego"). The search sees only the grid, where the cell beyond the
// border is out of bounds and so reads as wall, which is exactly the
// stop a slide into a door would wrongly look like. Same check
// player.c's updateRoomStep makes, cell for cell -- including reading
// cell.doorN/E/S/W rather than the grid: a door locked shut is already a
// wall in the grid, so the slide never gets there to begin with.
static bool ppDoor[4];
static u8 ppOff[4];

static void loadPlantPathDoors(u8 col, u8 row)
{
    const MapCell cell = guideMap[row][col];
    const bool isLink = (col == insertLinkCol) && (row == insertLinkRow);
    u8 d;

    ppDoor[DOOR_N] = cell.doorN || (isLink && (insertLinkDir == DOOR_N));
    ppDoor[DOOR_E] = cell.doorE || (isLink && (insertLinkDir == DOOR_E));
    ppDoor[DOOR_S] = cell.doorS || (isLink && (insertLinkDir == DOOR_S));
    ppDoor[DOOR_W] = cell.doorW || (isLink && (insertLinkDir == DOOR_W));
    for (d = 0; d < 4; d++)
        ppOff[d] = doorOffsetFor(col, row, d);
}

// (x,y) is where a slide in direction d came to a halt against the grid.
// TRUE when that halt isn't one: the ship sails through the door there.
static bool plantPathSlideExits(s16 x, s16 y, u8 d)
{
    if (!ppDoor[d])
        return FALSE;

    switch (d)
    {
        case DOOR_N: return (y == 0)          && ((x == ppOff[DOOR_N]) || (x == ppOff[DOOR_N] + 1));
        case DOOR_E: return (x == MAZE_W - 1) && ((y == ppOff[DOOR_E]) || (y == ppOff[DOOR_E] + 1));
        case DOOR_S: return (y == MAZE_H - 1) && ((x == ppOff[DOOR_S]) || (x == ppOff[DOOR_S] + 1));
        default:     return (x == 0)          && ((y == ppOff[DOOR_W]) || (y == ppOff[DOOR_W] + 1));
    }
}

// What the route aims at: anything in this room still worth crossing.
// That's every plant still standing and, since the user asked for it
// ("haz que el path tambien indique el path a la letra"), the room's own
// letter -- which always sits on the hub cell, the one place items.c ever
// draws it. Both are collected by passing OVER them, which is why the
// search can treat them as the same kind of target: the first slide that
// crosses either one wins, so what gets drawn is the nearest of the two
// and the route keeps working once a room's plants are all gone and only
// its letter is left.
static bool plantPathTargetAt(u8 roomCol, u8 roomRow, s16 x, s16 y)
{
    char letter;

    if (Plants_uncollectedAt(roomCol, roomRow, x, y))
        return TRUE;

    return (x == MAZE_DOOR_COL) && (y == MAZE_DOOR_ROW) &&
           Items_uncollectedAt(roomCol, roomRow, &letter);
}

static void clearPlantPathMarks(void)
{
    u8 i;

    for (i = 0; i < plantPathMarkCount; i++)
        VDP_drawTextBG(BG_B, " ", plantPathMarkX[i], plantPathMarkY[i]);
    plantPathMarkCount = 0;
}

// One route cell. Same rows 0/1 guard the hitbox gizmo uses -- those two
// belong to the FPS counter and the plant/letter HUD.
static void markPlantPathCell(s16 cx, s16 cy, u8 dir)
{
    static const char *const arrow[5] = { "^", ">", "v", "<", "o" }; // 4 = the route's own start cell
    const u16 tx = (u16) (Maze_originColumn() + cx);
    const u16 ty = (u16) (Maze_originRow() + cy); // room cells -> screen tiles, 1:1 now (maze.h)

    if (plantPathMarkCount >= PLANTPATH_MAX_MARKS)
        return;

    VDP_setTextPriority(1);
    VDP_drawTextBG(BG_B, arrow[dir], tx, ty);
    VDP_setTextPriority(0);
    plantPathMarkX[plantPathMarkCount] = tx;
    plantPathMarkY[plantPathMarkCount] = ty;
    plantPathMarkCount++;
}

// Draws one leg of the route: every cell the ship crosses sliding from
// (x,y) in dir, stopping either at the wall or at (stopX,stopY) when that
// is where this leg is meant to end.
static void markPlantPathLeg(s16 x, s16 y, u8 dir, s16 stopX, s16 stopY)
{
    for (;;)
    {
        const s16 nx = (s16) (x + ppDX[dir]), ny = (s16) (y + ppDY[dir]);

        if (Maze_isWall(nx, ny))
            return;
        x = nx; y = ny;
        markPlantPathCell(x, y, dir);
        if ((x == stopX) && (y == stopY))
            return;
    }
}

// Walks the predecessor chain back to the start, drawing each leg.
static void markPlantPathChain(s16 x, s16 y, s16 startX, s16 startY)
{
    while ((x != startX) || (y != startY))
    {
        const u8 dir = (u8) ppFromDir[y][x];
        const s16 px = (s16) ppFromX[y][x], py = (s16) ppFromY[y][x];

        markPlantPathLeg(px, py, dir, x, y);
        x = px; y = py;
    }
}

// Call once per frame while the mode is on. Shows nothing at all while the
// ship is sliding; the route appears once it stops, and is recomputed only
// then -- when it stopped somewhere new, or when something marked it dirty
// (a plant taken, a room loaded, the mode just switched on). Standing
// still on an unchanged route costs nothing.
// The one slide-flood both PLANTA and the map screen's own sweep run,
// starting from the cell (startX,startY) the ship is standing on (or,
// for the sweep/viewer, the cell it would be standing on having just
// come in through a door).
//
//   reachMode FALSE -- the PLANTA route: breadth-first over SLIDES (each
//     step a full slide until a wall, the only move the ship has),
//     stopping at the first one that CROSSES something still to collect
//     (plantPathTargetAt: a plant still standing or the room's letter --
//     either is taken by passing over it, not by stopping on it). The
//     winning route is drawn cell by cell, each an arrow showing which
//     way to push there. Returns TRUE when there was one.
//   reachMode TRUE -- the sweep: draws nothing, never stops early, and
//     marks in ppCrossed every cell any slide can cross. Always FALSE.
//     Cells of a slide that ends OUT of the room still count as crossed (the
//     ship does pass over them, collecting whatever is there, on its way
//     out); it is only the far side of that doorway that isn't a place
//     the search may carry on from.
//
// Searching the real grid with Maze_isWall rather than the slide graph:
// the graph is seeded from the room's doors and still misses a couple of
// resting cells per room, and this needs to start from wherever the ship
// happens to be, which is frequently not one of them.
static bool plantPathFlood(u8 roomCol, u8 roomRow, s16 startX, s16 startY, bool reachMode)
{
    s16 head = 0, tail = 0;
    s16 x, y;

    if (Maze_isWall(startX, startY))
        return FALSE;

    loadPlantPathDoors(roomCol, roomRow);

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
        {
            ppFromDir[y][x] = -1;
            if (reachMode)
                ppCrossed[y][x] = FALSE;
        }

    ppFromDir[startY][startX] = 4; // reached, but it is the start: no leg behind it
    ppQX[tail] = (u8) startX; ppQY[tail] = (u8) startY; tail++;
    if (reachMode)
        ppCrossed[startY][startX] = TRUE;

    while (head < tail)
    {
        const s16 cx = (s16) ppQX[head], cy = (s16) ppQY[head];
        u8 d;

        head++;
        for (d = 0; d < 4; d++)
        {
            s16 wx = cx, wy = cy;
            bool found = FALSE;

            for (;;)
            {
                const s16 nx = (s16) (wx + ppDX[d]), ny = (s16) (wy + ppDY[d]);

                if (Maze_isWall(nx, ny))
                    break;
                wx = nx; wy = ny;
                if (reachMode)
                    ppCrossed[wy][wx] = TRUE;
                else if (plantPathTargetAt(roomCol, roomRow, wx, wy))
                {
                    found = TRUE;
                    break;
                }
            }

            if (found)
            {
                // Final leg first (it stops ON the target, not at the wall),
                // then everything that led here.
                markPlantPathLeg(cx, cy, d, wx, wy);
                markPlantPathChain(cx, cy, startX, startY);
                return TRUE;
            }

            // The slide ends in a doorway: it doesn't end at all, the ship
            // leaves the room. Not a move within this room, so neither a
            // leg nor a place to carry on searching from.
            if (plantPathSlideExits(wx, wy, d))
                continue;

            if (((wx != cx) || (wy != cy)) && (ppFromDir[wy][wx] < 0))
            {
                ppFromDir[wy][wx] = (s8) d;
                ppFromX[wy][wx] = (u8) cx;
                ppFromY[wy][wx] = (u8) cy;
                ppQX[tail] = (u8) wx; ppQY[tail] = (u8) wy; tail++;
            }
        }
    }

    return FALSE;
}

// Call once per frame while the mode is on. Shows nothing at all while the
// ship is sliding; the route appears once it stops, and is recomputed only
// then -- when it stopped somewhere new, or when something marked it dirty
// (a plant taken, a room loaded, the mode just switched on). Standing
// still on an unchanged route costs nothing.
static void drawPlantPath(void)
{
    const s16 startX = (s16) (player.x / MAZE_TILE_PX);
    const s16 startY = (s16) (player.y / MAZE_TILE_PX);

    if (!plantPathOn || (gameState != STATE_PLAYING))
        return;

    // A map-screen tool is up: it owns the arrows (the room viewer draws
    // its own route with them, from a door instead of from the ship), so
    // this keeps its hands off entirely -- wiping them here would erase
    // the viewer's route the very frame it drew it. Leaving the tool puts
    // the live room back and marks the route dirty itself.
    if (roomViewerOn || sweepOn)
        return;

    // Map overlay open, or the insertion room: nothing to route to, and
    // the arrows would sit on top of the map (they are high-priority
    // BG_B, the overlay is BG_A). Wipe them and leave the route dirty so
    // it comes back by itself once the view does.
    if (mapViewOpen || inInsertRoom)
    {
        clearPlantPathMarks();
        plantPathDirty = TRUE;
        return;
    }

    // Only while the ship is stopped against whatever halted it (user
    // request). Mid-slide the ship crosses a new cell every few frames and
    // the answer changes with it, so the arrows streak past and the search
    // runs over and over for positions the player never gets to act from.
    // tombMoving is "the ship changed position last frame", so !tombMoving
    // is exactly the ship sitting still -- which in this game means parked
    // against a wall, since a slide only ends by hitting one.
    if (tombMoving)
    {
        clearPlantPathMarks();
        plantPathDirty = TRUE;
        return;
    }

    if (!plantPathDirty && (startX == plantPathLastX) && (startY == plantPathLastY))
        return;

    plantPathDirty = FALSE;
    plantPathLastX = startX;
    plantPathLastY = startY;

    clearPlantPathMarks();
    plantPathFlood(currentCol, currentRow, startX, startY, FALSE);
}

// Everything on this planet collected -- letters AND plants (user
// request: "se muestra cuando la nave recogio letras y plantas en su
// totalidad") -- so tell the player to take it all back.
//
// It pulses once every RETURN_MSG_PERIOD frames (user request: "el
// parpadeo cada 10 seg") instead of breathing without pause: the glyphs
// are drawn through PAL2 -- free during play, since the enemy sprite only
// ever reads that palette's index 1 and SGDK's font reads index 15 -- and
// what animates is that one colour, up from black and back down through
// the 8 greys the VDP's 3 bits per channel can actually make. Black is
// invisible against the backdrop, so between pulses there is simply
// nothing there, and the pulse itself fades in and out instead of
// snapping on.
static void drawReturnToInsertHint(void)
{
    static const u32 ramp[8] = { 0x000000, 0x242424, 0x484848, 0x6D6D6D,
                                  0x919191, 0xB6B6B6, 0xDADADA, 0xFFFFFF };
    const bool want = (gameState == STATE_PLAYING) && !inInsertRoom &&
                      Items_allCollected() &&
                      (Plants_collectedCount() >= presetSave[sizePresetIndex].plantsTotal) &&
                      !mapViewOpen && !roomViewerOn && !sweepOn;

    if (!want)
    {
        if (returnMsgShown)
        {
            VDP_clearTextLineBG(BG_B, RETURN_MSG_ROW);
            returnMsgShown = FALSE;
        }
        return;
    }

    // Redrawn every frame rather than once: cheap at 13 glyphs, and it
    // survives everything that wipes BG_B behind its back (the sweep
    // screen, resetToMenu) without having to know about any of them.
    VDP_setTextPriority(1);
    VDP_setTextPalette(PAL2);
    VDP_drawTextBG(BG_B, RETURN_MSG_TEXT, (40 - RETURN_MSG_LEN) / 2, RETURN_MSG_ROW);
    VDP_setTextPalette(PAL0);
    VDP_setTextPriority(0);
    returnMsgShown = TRUE;

    {
        const u16 t = (u16) (returnMsgPhase % RETURN_MSG_PERIOD);
        const u16 up = (u16) (8 * RETURN_MSG_HOLD);
        // Outside the pulse it sits on ramp[0] -- black, invisible,
        // waiting for the next one.
        const u8 step = (t < RETURN_MSG_PULSE)
                            ? (u8) (((t < up) ? t : (u16) (RETURN_MSG_PULSE - 1 - t)) / RETURN_MSG_HOLD)
                            : 0;

        PAL_setColor(RETURN_MSG_INK_INDEX, RGB24_TO_VDPCOLOR(ramp[step]));
        returnMsgPhase++;
    }
}

// One door's live state, read straight off the grid rather than off the
// lock logic -- '.' no door there, 'o' open, '#' walled. Reading the grid
// is deliberate: it reports what the player is actually looking at, so a
// disagreement between this and what the lock logic intended shows up
// instead of being hidden.
static char doorStateChar(u8 col, u8 row, u8 dir)
{
    const MapCell cell = guideMap[row][col];
    const bool isLink = (col == insertLinkCol) && (row == insertLinkRow);
    bool exists;
    s16 x, y;
    u8 off;

    switch (dir)
    {
        case DOOR_N: exists = cell.doorN || (isLink && (insertLinkDir == DOOR_N)); break;
        case DOOR_E: exists = cell.doorE || (isLink && (insertLinkDir == DOOR_E)); break;
        case DOOR_S: exists = cell.doorS || (isLink && (insertLinkDir == DOOR_S)); break;
        default:     exists = cell.doorW || (isLink && (insertLinkDir == DOOR_W)); break;
    }

    if (!exists)
        return '.';

    off = doorOffsetFor(col, row, dir);
    switch (dir)
    {
        case DOOR_N: x = off;          y = 0;          break;
        case DOOR_S: x = off;          y = MAZE_H - 1; break;
        case DOOR_E: x = MAZE_W - 1;   y = off;        break;
        default:     x = 0;            y = off;        break; // DOOR_W
    }

    return Maze_isWall(x, y) ? '#' : 'o';
}

// Always-on room identity readout (user request: "pinta siempre la seed
// room etc, todo lo que necesites para debugar"). Everything needed to
// regenerate THIS exact room off-line and reason about it:
//
//   P<n>     which planet preset -- gives mapCols/mapRows/letters
//   M:<hex>  the map seed -- GuideMap_generate() is reproducible from it
//   R:<hex>  this room's own seed (roomSeedFor), a cross-check on the two above
//   <c>,<r>  the room's grid coordinates
//   NESW     each door: '.' none, 'o' open, '#' walled
//   L<0|1>   whether the room counts as cleared (plants + letter all taken)
//
// BG_B row 0, left of the FPS counter, same plane/high-priority trick the
// rest of the HUD uses. Padded to a fixed width so a shorter line (the
// insertion room's) can never leave characters of a longer one behind.
// ---------------------------------------------------------------------
// The map screen's two debug tools (user request: "en el mapa, hay debug
// si pulsas B se ven todas las habitaciones", plus the route sweep).
// They hang off the map overlay (held A) rather than the menu, where C
// is already the sound toggle -- here START+B and START+C are free (the
// lone B next to them lifts the map's fog, see that call site). Neither
// tool can run in the insertion room: the map itself doesn't open there
// (its own !inInsertRoom guard), and guideMap/currentCol/currentRow mean
// nothing while it's up.
// ---------------------------------------------------------------------

// Everything Maze_generateRoom needs for one room, worked out exactly
// the way loadRoom()/applyRoomDoorLocks() do -- but READ-ONLY: no door
// latched open, no `visited` bit, no enemy. See ROOM_LOCKS_LIVE below
// for what lockMode picks.
typedef struct
{
    bool door[4];   // indexed by guidemap.h's DOOR_N/E/S/W, like everything else here
    bool locked[4];
    u8 off[4];
    u16 seed;
    u8 slot;
} RoomBuild;

// lockMode for roomBuildFor/buildRoomForInspection: the room's own live
// lock state, or the one state that actually matters for an audit --
// exactly one door open, which is how a fresh room is always met (the
// one the ship came in by is latched open, every other one is still
// sealed until the room clears).
#define ROOM_LOCKS_LIVE 0xFF

static void roomBuildFor(u8 col, u8 row, u8 lockMode, RoomBuild *out)
{
    const MapCell cell = guideMap[row][col];
    const bool isLink = (col == insertLinkCol) && (row == insertLinkRow);
    const bool cleared = roomFullyCleared(col, row);
    bool orderLocked[4];
    u8 d;

    out->door[DOOR_N] = cell.doorN || (isLink && (insertLinkDir == DOOR_N));
    out->door[DOOR_E] = cell.doorE || (isLink && (insertLinkDir == DOOR_E));
    out->door[DOOR_S] = cell.doorS || (isLink && (insertLinkDir == DOOR_S));
    out->door[DOOR_W] = cell.doorW || (isLink && (insertLinkDir == DOOR_W));

    // Same short-circuit applyRoomDoorLocks relies on: an edge with no
    // door in the tree never reads its (possibly out-of-range) neighbor.
    orderLocked[DOOR_N] = cell.doorN && GuideMap_isRoomLocked(col, row - 1);
    orderLocked[DOOR_E] = cell.doorE && GuideMap_isRoomLocked(col + 1, row);
    orderLocked[DOOR_S] = cell.doorS && GuideMap_isRoomLocked(col, row + 1);
    orderLocked[DOOR_W] = cell.doorW && GuideMap_isRoomLocked(col - 1, row);

    for (d = 0; d < 4; d++)
    {
        const bool open = out->door[d] &&
                          ((lockMode == ROOM_LOCKS_LIVE)
                               ? (doorIsLatchedOpen(col, row, d) || (cleared && !orderLocked[d]))
                               : (d == lockMode));

        out->off[d] = doorOffsetFor(col, row, d);
        out->locked[d] = !open;
    }

    out->seed = roomSeedFor(col, row);
    out->slot = (u8) ((row * MAX_MAP_COLS) + col);
}

// Puts room (col,row) into maze.c's grid and plants.c's plant positions
// through the exact same two passes loadRoom() uses (first with every
// door sealed -- the grid plants are placed against -- then again with
// whatever lock state lockMode asks for), so what comes out is identical
// to what the player meets walking in. Nothing persistent is touched, so
// putting the live room back afterwards is just another call for it.
static void buildRoomForInspection(u8 col, u8 row, u8 lockMode)
{
    RoomBuild rb;

    roomBuildFor(col, row, lockMode, &rb);
    Maze_generateRoom(rb.door[DOOR_N], rb.door[DOOR_E], rb.door[DOOR_S], rb.door[DOOR_W],
                       rb.door[DOOR_N], rb.door[DOOR_E], rb.door[DOOR_S], rb.door[DOOR_W],
                       rb.off, rb.seed, rb.slot);
    Plants_spawnForRoom(rb.seed);
    Maze_generateRoom(rb.door[DOOR_N], rb.door[DOOR_E], rb.door[DOOR_S], rb.door[DOOR_W],
                       rb.locked[DOOR_N], rb.locked[DOOR_E], rb.locked[DOOR_S], rb.locked[DOOR_W],
                       rb.off, rb.seed, rb.slot);
}

// Where the ship first stands having come in through door `dir`: the cell
// just inside that border, on the door's own 2-cell span (spec §30).
// FALSE when the room has no door there, or both of the span's inner
// cells read as wall.
static bool roomEntryCell(const RoomBuild *rb, u8 dir, s16 *outX, s16 *outY)
{
    u8 i;

    if (!rb->door[dir])
        return FALSE;

    for (i = 0; i < 2; i++)
    {
        const s16 along = (s16) (rb->off[dir] + i);
        s16 x, y;

        switch (dir)
        {
            case DOOR_N: x = along;          y = 1;              break;
            case DOOR_E: x = MAZE_W - 2;     y = along;          break;
            case DOOR_S: x = along;          y = MAZE_H - 2;     break;
            default:     x = 1;              y = along;          break;
        }

        if (!Maze_isWall(x, y))
        {
            *outX = x; *outY = y;
            return TRUE;
        }
    }

    return FALSE;
}

// Both tools take the whole screen, so both leave it the same way: the
// live room rebuilt and redrawn, the two HUD rows repainted, the ship
// (and whatever is left of the enemy) visible again.
static void restoreLiveRoomAfterDebugScreen(void)
{
    buildRoomForInspection(currentCol, currentRow, ROOM_LOCKS_LIVE);
    Maze_draw();
    Items_drawInRoom(currentCol, currentRow);
    Plants_drawInRoom(currentCol, currentRow);

    // The tools' own text is longer than what the HUDs write back, so the
    // row goes entirely before they repaint it.
    VDP_clearTextLineBG(BG_B, 1);
    Items_drawHud();
    Plants_drawHud();

    clearPlantPathMarks(); // the viewer's own route, drawn with the same marks
    PAL_setColor(PLAYER_SHIP_INK_INDEX, PLAYER_SHIP_COLOR); // the map overlay left it white
    SPR_setVisibility(playerSprite, VISIBLE);
    SPR_setVisibility(mapShipSprite, HIDDEN);
    SPR_setVisibility(enemySprite, Enemy_blinkVisible(&enemy) ? VISIBLE : HIDDEN);
    plantPathDirty = TRUE; // whatever PLANTA had cached is from before all this
}

// Shared entry: the map overlay is up and B or C was just pressed. Shuts
// the map down by hand (releasing A later must not repaint anything over
// the tool) and clears the screen for whichever tool asked.
static void beginDebugScreen(void)
{
    if (mapShowingNoise)
    {
        PSG_setEnvelope(3, PSG_ENVELOPE_MIN); // the static's own hiss, silenced the same way releasing A does
        VDP_clearTextLineBG(BG_B, NO_MAP_SIGNAL_ROW);
    }

    mapViewOpen = FALSE;
    mapShowingNoise = FALSE;
    SPR_setVisibility(playerSprite, HIDDEN);
    SPR_setVisibility(mapShipSprite, HIDDEN);
    SPR_setVisibility(enemySprite, HIDDEN);
    PAL_setColor(PLAYER_SHIP_INK_INDEX, PLAYER_SHIP_COLOR);
    clearPlantPathMarks();
}

// --- B: the room viewer ----------------------------------------------
// Walks the planet's rooms with the D-pad, drawing each one exactly as it
// would be walked into -- the grid, its letter, its plants still standing
// -- without the ship being there and without the visit counting for
// anything. It also draws that room's route, from the cell the ship
// would stand on having just come in through one of its doors (A cycles
// which, since there is no ship to start from here).
static u8 rvCol, rvRow;
static u8 rvEntryDir; // which door's arrival cell the PLANTA route starts from

static void drawRoomViewer(void)
{
    RoomBuild rb;
    char buf[40];
    int len;
    s16 sx, sy;

    roomBuildFor(rvCol, rvRow, ROOM_LOCKS_LIVE, &rb);
    buildRoomForInspection(rvCol, rvRow, ROOM_LOCKS_LIVE);
    Maze_draw();
    Items_drawInRoom(rvCol, rvRow);
    Plants_drawInRoom(rvCol, rvRow);

    clearPlantPathMarks();
    // Always, not gated on the in-game route button any more: that one is
    // held now, and this screen is reached with A held, so it could never
    // be on by the time the viewer opens.
    if (roomEntryCell(&rb, rvEntryDir, &sx, &sy))
    {
        // Same arrows the live overlay draws, just from a door's arrival
        // cell instead of the ship's own. The start is marked too -- with
        // no ship on screen there'd otherwise be no telling where the
        // route begins.
        plantPathFlood(rvCol, rvRow, sx, sy, FALSE);
        markPlantPathCell(sx, sy, 4);
    }

    len = sprintf(buf, "VISOR %02d,%02d S:%04X %c%c%c%c E:%c B:SAL", rvCol, rvRow,
                  roomSeedFor(rvCol, rvRow),
                  doorStateChar(rvCol, rvRow, DOOR_N), doorStateChar(rvCol, rvRow, DOOR_E),
                  doorStateChar(rvCol, rvRow, DOOR_S), doorStateChar(rvCol, rvRow, DOOR_W),
                  "NESO"[rvEntryDir]);
    while (len < 39)
        buf[len++] = ' ';
    buf[len] = 0;
    VDP_setTextPriority(1);
    VDP_drawTextBG(BG_B, buf, 0, 1);
    VDP_setTextPriority(0);
}

static void enterRoomViewer(void)
{
    beginDebugScreen();
    roomViewerOn = TRUE;
    rvCol = currentCol;
    rvRow = currentRow;
    rvEntryDir = DOOR_N;
    drawRoomViewer();
}

// D-pad steps one room along the grid (never off it, and never onto a
// cell the map has no room in); A cycles which door the PLANTA route is
// traced from; B puts the game back.
static void updateRoomViewer(u16 state, u16 prevState)
{
    s16 nc = rvCol, nr = rvRow;

    if ((state & BUTTON_B) && !(prevState & BUTTON_B))
    {
        roomViewerOn = FALSE;
        restoreLiveRoomAfterDebugScreen();
        return;
    }

    if ((state & BUTTON_A) && !(prevState & BUTTON_A))
    {
        rvEntryDir = (u8) ((rvEntryDir + 1) & 3);
        drawRoomViewer();
        return;
    }

    if ((state & BUTTON_UP) && !(prevState & BUTTON_UP)) nr--;
    else if ((state & BUTTON_DOWN) && !(prevState & BUTTON_DOWN)) nr++;
    else if ((state & BUTTON_LEFT) && !(prevState & BUTTON_LEFT)) nc--;
    else if ((state & BUTTON_RIGHT) && !(prevState & BUTTON_RIGHT)) nc++;
    else return;

    if ((nc < 0) || (nr < 0) || (nc >= mapCols) || (nr >= mapRows))
        return;
    if (guideMap[nr][nc].type != CELL_ROOM)
        return;

    rvCol = (u8) nc;
    rvRow = (u8) nr;
    drawRoomViewer();
}

// --- C: the route sweep ----------------------------------------------
// Runs the PLANTA search over every room of THIS planet (only this one:
// another planet's map would mean overwriting guideMap, which is the
// live game's own state) and reports the plants a room can fail to give
// up -- the in-game regression test for maze.c's plant placement.
//
// Same criterion maze.c's computePlantSafe places against, so the two
// agree by construction: per door, rebuild the room the way arriving
// through it really leaves it (only that one open, the rest sealed --
// the state a fresh room is always met in), lets the ship in on each half
// of the doorway in turn (sweepArrivalRest -- still moving, so its first
// stop is the end of the slide straight in) and floods every slide that
// stays in the room. INTERSECT over the doors, not union: the room graph is a
// tree, so before a room clears, the only door it can ever be entered by
// is the one facing the start room, and neither this nor maze.c knows
// which that is. A plant outside the intersection is one that, for some
// entry, cannot be collected -- so that room may never clear, its doors
// never open, and the branch behind it never be reachable.
//
// With the placement fixed this reports nothing; it is here to say so,
// and to catch it if that ever stops being true.
//
// One (room, door) pair per frame: each flood is a full breadth-first
// walk of the room, far too much to do a whole planet's worth of in one.
#define SWEEP_FIRST_REPORT_ROW 7
#define SWEEP_LAST_REPORT_ROW 24

static bool sweepDone;
static u16 sweepCursor; // room index into the map grid, mapCols-major like introTick's
static u8 sweepDoor;
static u16 sweepRooms, sweepBadRooms, sweepBadPlants;
static u8 sweepReportRow;
// The running intersection over every arrival checked so far (each door,
// each half of its doorway), which is what the room is judged on once the
// fourth door has had its turn.
static bool sweepSafe[MAZE_MAX_H][MAZE_MAX_W];
// Just the cells one arrival slide crosses on its way in, before the flood
// from where it stops takes over.
static bool sweepArrival[MAZE_MAX_H][MAZE_MAX_W];
static bool sweepFirstDoor;

static void enterSweep(void)
{
    beginDebugScreen();
    sweepOn = TRUE;
    sweepDone = FALSE;
    sweepCursor = 0;
    sweepDoor = 0;
    sweepRooms = 0;
    sweepBadRooms = 0;
    sweepBadPlants = 0;
    sweepFirstDoor = TRUE;
    sweepReportRow = SWEEP_FIRST_REPORT_ROW;

    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    invalidateHudLines();
    VDP_drawText("BARRIDO DE RUTAS", 12, 2);
    VDP_drawText("PLANTAS QUE ALGUNA ENTRADA NO ALCANZA", 2, 3);
}

// The ship arrives through door `dir` still moving, so its first resting
// place is wherever the slide straight in ends -- not the threshold.
// Marks the cells that run crosses into sweepDoorReach and hands back
// that first stop. FALSE when the arrival never stops at all (straight in
// and straight out the far side).
//
// `lane` is which half of the doorway the ship comes out on -- it keeps
// the one it left the previous room by (doorLanePx), so both are real
// arrivals and the caller judges each on its own. Checking them and
// keeping the UNION is what let this sweep call a room clean while 3 of
// its plants had no route from one of the two.
static bool sweepArrivalRest(const RoomBuild *rb, u8 dir, u8 lane, s16 *outX, s16 *outY)
{
    const u8 inward = (u8) ((dir + 2) & 3);
    s16 x, y;

    if (!rb->door[dir])
        return FALSE;

    switch (dir)
    {
        case DOOR_N: x = (s16) (rb->off[dir] + lane); y = 1;                          break;
        case DOOR_E: x = MAZE_W - 2;                  y = (s16) (rb->off[dir] + lane); break;
        case DOOR_S: x = (s16) (rb->off[dir] + lane); y = MAZE_H - 2;                  break;
        default:     x = 1;                           y = (s16) (rb->off[dir] + lane); break;
    }

    if (Maze_isWall(x, y))
        return FALSE;

    {
        s16 cx, cy;

        for (cy = 0; cy < MAZE_H; cy++)
            for (cx = 0; cx < MAZE_W; cx++)
                sweepArrival[cy][cx] = FALSE;
    }
    sweepArrival[y][x] = TRUE;

    for (;;)
    {
        const s16 nx = (s16) (x + ppDX[inward]), ny = (s16) (y + ppDY[inward]);

        if (Maze_isWall(nx, ny))
            break;
        x = nx; y = ny;
        sweepArrival[y][x] = TRUE;
        if (plantPathSlideExits(x, y, inward))
            return FALSE; // in one door and out the opposite one, never stopping
    }

    *outX = x; *outY = y;
    return TRUE;
}

// One (room, door) pair. Called once per frame while the sweep runs.
static void sweepStep(void)
{
    const u16 cellCount = (u16) (mapCols * mapRows);
    const u8 col = (u8) (sweepCursor % mapCols);
    const u8 row = (u8) (sweepCursor / mapCols);
    RoomBuild rb;
    char buf[40];
    s16 x, y;
    u8 lane;

    if (sweepCursor >= cellCount)
    {
        sprintf(buf, "FIN. SALAS:%d  SALAS CON FALLO:%d", sweepRooms, sweepBadRooms);
        VDP_drawText(buf, 2, 5);
        sprintf(buf, "PLANTAS SIN RUTA:%d", sweepBadPlants);
        VDP_drawText(buf, 2, 26);
        VDP_drawText("PULSA B PARA VOLVER", 2, 27);
        sweepDone = TRUE;
        return;
    }

    if (guideMap[row][col].type != CELL_ROOM)
    {
        sweepCursor++;
        sweepDoor = 0;
        sweepFirstDoor = TRUE;
        return;
    }

    sprintf(buf, "SALA %02d,%02d  PUERTA %c  (%d/%d)", col, row, "NESO"[sweepDoor],
            sweepCursor + 1, cellCount);
    VDP_drawText(buf, 2, 5);

    // Rebuilt every frame: each door is audited against its own lock state
    // anyway, and a generation replays from maze.c's accepted-attempt
    // cache, so it's cheap.
    roomBuildFor(col, row, sweepDoor, &rb);
    buildRoomForInspection(col, row, sweepDoor);
    loadPlantPathDoors(col, row);

    // Intersection over every arrival this room has -- each door, and each
    // half of its doorway. A plant has to survive all of them: which one
    // the player turns up on is not something either this or maze.c can
    // know (see the doc comment).
    for (lane = 0; lane < 2; lane++)
    {
        s16 sx, sy;

        if (!sweepArrivalRest(&rb, sweepDoor, lane, &sx, &sy))
            continue;

        // The arrival slide's own cells count as crossed too: sweepArrivalRest
        // marked them, and plantPathFlood's ppCrossed starts from the cell it
        // came to rest on.
        plantPathFlood(col, row, sx, sy, TRUE);
        for (y = 0; y < MAZE_H; y++)
            for (x = 0; x < MAZE_W; x++)
            {
                const bool reached = ppCrossed[y][x] || sweepArrival[y][x];

                sweepSafe[y][x] = sweepFirstDoor ? reached : (sweepSafe[y][x] && reached);
            }
        sweepFirstDoor = FALSE;
    }

    sweepDoor++;
    if (sweepDoor < 4)
        return;

    // Every door has had its turn.
    {
        // sweepFirstDoor still set means not one door of this room ever
        // produced an arrival -- nothing is reachable, so every plant in
        // it counts.
        const bool anyArrival = !sweepFirstDoor;
        u16 missing = 0;

        for (y = 0; y < MAZE_H; y++)
            for (x = 0; x < MAZE_W; x++)
                if (Plants_uncollectedAt(col, row, x, y) && (!anyArrival || !sweepSafe[y][x]))
                    missing++;

        if (missing > 0)
        {
            sweepBadPlants = (u16) (sweepBadPlants + missing);
            sweepBadRooms++;
            if (sweepReportRow <= SWEEP_LAST_REPORT_ROW)
            {
                sprintf(buf, "R%02d,%02d: %d PLANTAS SIN RUTA", col, row, missing);
                VDP_drawText(buf, 2, sweepReportRow);
                sweepReportRow++;
            }
        }
    }

    sweepDoor = 0;
    sweepFirstDoor = TRUE;
    sweepCursor++;
    sweepRooms++;
}

static void updateSweep(u16 state, u16 prevState)
{
    if ((state & BUTTON_B) && !(prevState & BUTTON_B))
    {
        sweepOn = FALSE;
        restoreLiveRoomAfterDebugScreen();
        return;
    }

    if (!sweepDone)
        sweepStep();
}

// Both BG_B status lines below are rebuilt only when what they SAY
// changes, not every frame. They used to run a sprintf and a 31-glyph
// VDP_drawTextBG on every single frame of play, for text that only ever
// changes when the player crosses a door (the room line) or once a
// second (the FPS counter) -- on a 68000 an sprintf with nine
// conversions is thousands of cycles, and the glyphs are poked through
// the VDP port one word at a time while the display is active. Set this
// wherever something else wipes those lines, so the next frame puts them
// back.
static bool hudLinesDirty = TRUE;

static void invalidateHudLines(void)
{
    hudLinesDirty = TRUE;
}

// Top-right corner (user request: "el indicador de score estara en la
// derecha arriba, donde estan los FPS en debug"). Same cached-redraw rule
// the other BG_B lines use -- the number only changes once a second or
// on a move, so there is no reason to poke glyphs every frame.
#define SCORE_COL 32 // leaves 8 columns for the number, FPS goes left of it

static void drawScoreHud(void)
{
    static u32 prev = 0xFFFFFFFF;
    const u32 score = scoreFor(sizePresetIndex);
    char buf[12];
    int len;

    if (gameState != STATE_PLAYING)
    {
        prev = 0xFFFFFFFF; // nothing of ours is on screen any more
        return;
    }

    if ((score == prev) && !hudLinesDirty)
        return;

    prev = score;
    len = sprintf(buf, "%lu", score);

    VDP_setTextPriority(1);
    // Right-aligned against the screen edge, padded so a shrinking number
    // never leaves its old digits behind.
    VDP_drawTextBG(BG_B, "        ", SCORE_COL, 0);
    VDP_drawTextBG(BG_B, buf, (u16) (40 - len), 0);
    VDP_setTextPriority(0);
}

static void drawRoomIdHud(void)
{
    static char prev[40];
    static bool prevValid = FALSE;
    char buf[40];
    int len;

    if (!debugInfoOn || (gameState != STATE_PLAYING))
    {
        prevValid = FALSE; // nothing of ours is on screen any more
        return;
    }

    if (inInsertRoom)
        len = sprintf(buf, "P%d M:%04X INS", sizePresetIndex, mapSeed);
    else
        len = sprintf(buf, "P%d M:%04X R:%04X %02d,%02d %c%c%c%c L%d",
                      sizePresetIndex, mapSeed, roomSeedFor(currentCol, currentRow),
                      currentCol, currentRow,
                      doorStateChar(currentCol, currentRow, DOOR_N),
                      doorStateChar(currentCol, currentRow, DOOR_E),
                      doorStateChar(currentCol, currentRow, DOOR_S),
                      doorStateChar(currentCol, currentRow, DOOR_W),
                      roomFullyCleared(currentCol, currentRow) ? 1 : 0);

    while (len < 31)
        buf[len++] = ' ';
    buf[len] = '\0';

    if (prevValid && !hudLinesDirty && !strcmp(buf, prev))
        return; // same text already on screen

    strcpy(prev, buf);
    prevValid = TRUE;

    VDP_setTextPriority(1);
    VDP_drawTextBG(BG_B, buf, 0, 0);
    VDP_setTextPriority(0);
}

// Toggled by the debug combo (Konami-style sequence, see debugCombo's own
// doc comment) -- wipes every row the panel below prints to, so turning
// the display off doesn't leave stale text sitting on BG_B.
static void clearDebugView(void)
{
    u8 row;

    for (row = DEBUG_VIEW_FIRST_ROW; row < (DEBUG_VIEW_FIRST_ROW + DEBUG_VIEW_ROWS); row++)
        VDP_clearTextLineBG(BG_B, row);
    // Undoes Maze_drawDebugBackdrop's solid box on BG_A (repaints the real
    // maze over the whole screen, which covers those rows too) -- only
    // when BG_A actually holds the maze. In STATE_MENU (and STATE_INTRO,
    // its own boot-time prompt/terminal screen), BG_A holds something
    // else (drawMenu()/drawIntroPrompt()/introBegin()'s own
    // VDP_drawText calls), and Maze_draw() would stomp it with whatever's
    // left in maze.c's grid[][] from the last room visited (or never
    // generated at all, this boot); Maze_drawDebugBackdrop below never
    // painted over either in the first place for the same reason, so
    // there's nothing to undo there.
    if ((gameState != STATE_MENU) && (gameState != STATE_INTRO))
    {
        Maze_draw();
        // Same letter/plants wipe as DEBUG_EDGES_COMBO's own toggle (see
        // its comment) -- Maze_draw() alone doesn't redraw either.
        Items_drawInRoom(currentCol, currentRow);
        Plants_drawInRoom(currentCol, currentRow);
    }
}

// Call once per frame with this frame's joypad state, regardless of
// gameState -- works from the menu too, same as RESET_COMBO above.
static void updateDebugCombo(u16 state, u16 prevState)
{
    if (((state & DEBUG_VIEW_COMBO) == DEBUG_VIEW_COMBO) && ((prevState & DEBUG_VIEW_COMBO) != DEBUG_VIEW_COMBO))
    {
        debugViewOn = !debugViewOn;
        if (debugViewOn)
        {
            // See clearDebugView()'s comment: BG_A holds the menu or the
            // STATE_INTRO screen, not the maze, in those two states --
            // skip the backdrop there so the text still just overlays
            // whatever's underneath (unreadable-over-menu wasn't the
            // complaint; unreadable-over-the-moving-maze was).
            if ((gameState != STATE_MENU) && (gameState != STATE_INTRO))
                Maze_drawDebugBackdrop(DEBUG_VIEW_FIRST_ROW, DEBUG_VIEW_ROWS);
        }
        else
            clearDebugView();
    }

    // DEBUG_EDGES_COMBO (plain B+C) is a subset of DEBUG_VIEW_COMBO/
    // RESET_COMBO above -- the exclude mask confirms
    // no direction/A is ALSO held, so this only fires for B+C alone, not
    // every time one of those other three chords also happens to include
    // B+C.
    if (((state & (DEBUG_EDGES_COMBO | DEBUG_EDGES_EXCLUDE)) == DEBUG_EDGES_COMBO) &&
        ((prevState & (DEBUG_EDGES_COMBO | DEBUG_EDGES_EXCLUDE)) != DEBUG_EDGES_COMBO))
    {
        // Same STATE_MENU/STATE_INTRO guard as the backdrop above: BG_A
        // isn't the maze in either, and Maze_draw() is what actually
        // paints (or erases, on the next toggle) the edges overlay.
        if ((gameState != STATE_MENU) && (gameState != STATE_INTRO))
        {
            Maze_setDebugEdgesVisible(!Maze_debugEdgesVisible());
            // Maze_draw() repaints every tile of BG_A from scratch (same
            // "wipes the letter/plants" situation its other call sites
            // already comment on) -- without these two, toggling RUTAS
            // off blanked this room's letter and plants (user report:
            // "al sacar este modo... se borran").
            Maze_draw();
            Items_drawInRoom(currentCol, currentRow);
            Plants_drawInRoom(currentCol, currentRow);
        }
    }

    if (((state & DEBUG_HITBOX_COMBO) == DEBUG_HITBOX_COMBO) && ((prevState & DEBUG_HITBOX_COMBO) != DEBUG_HITBOX_COMBO))
    {
        hitboxDebugOn = !hitboxDebugOn;
        if (!hitboxDebugOn)
            clearHitboxMarks(); // drawCollisionGizmos() (per-frame, STATE_PLAYING only) won't run again to do it itself
    }

    if (((state & DEBUG_INFO_COMBO) == DEBUG_INFO_COMBO) && ((prevState & DEBUG_INFO_COMBO) != DEBUG_INFO_COMBO))
    {
        debugInfoOn = !debugInfoOn;
        if (debugInfoOn)
            invalidateHudLines(); // both readouts share row 0: put them straight back
        else
            VDP_clearTextLineBG(BG_B, 0); // neither will run again to clear itself
    }
}

// The debug panel itself (user request: "quiero que hagas un display
// debug... y pintes en esa pantalla, a tiempo real, toda la informacion
// que consideres necesaria del estado del juego"). BG_B, high priority,
// same plane/trick as the FPS counter and HUD -- draws over BG_A without
// either needing to coordinate. Call once per frame; a no-op while
// debugViewOn is FALSE (clearDebugView already wiped the rows once, right
// when the combo turned it off).
static void drawDebugView(void)
{
    char buf[40];

    if (!debugViewOn)
        return;

    VDP_setTextPriority(1);

    {
        static const char *const stateNames[] = { "MENU", "PLAY", "WIN ", "OVER", "SCOR" };

        sprintf(buf, "ST:%s", stateNames[gameState]);
        VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW);
    }

    sprintf(buf, "COL:%d ROW:%d INS:%d MAPV:%d",
            currentCol, currentRow, inInsertRoom, mapViewOpen);
    VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW + 1);

    {
        const MapCell cell = guideMap[currentRow][currentCol];
        // Same short-circuit pattern loadRoom() uses -- only reads a
        // neighbor's lock state when a real door exists that way, so this
        // never indexes guideMap out of bounds at a grid edge.
        const bool lockedN = cell.doorN && GuideMap_isRoomLocked(currentCol, currentRow - 1);
        const bool lockedE = cell.doorE && GuideMap_isRoomLocked(currentCol + 1, currentRow);
        const bool lockedS = cell.doorS && GuideMap_isRoomLocked(currentCol, currentRow + 1);
        const bool lockedW = cell.doorW && GuideMap_isRoomLocked(currentCol - 1, currentRow);

        sprintf(buf, "DR N:%d/%d E:%d/%d S:%d/%d W:%d/%d (open/lock)",
                cell.doorN, lockedN, cell.doorE, lockedE, cell.doorS, lockedS, cell.doorW, lockedW);
        VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW + 2);
    }

    {
        static const char *const dirNames[] = { "UP", "LE", "DN", "RI", "--" };

        sprintf(buf, "PLR X:%d Y:%d TX:%d TY:%d D:%s",
                player.x, player.y, player.x / MAZE_TILE_PX, player.y / MAZE_TILE_PX, dirNames[player.dir]);
        VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW + 3);
    }

    sprintf(buf, "SPD:%d WBLK:%d SLDDST:%d", moveSpeedDebug, wasWallBlocked, slideDistance);
    VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW + 4);

    {
        static const char *const enemyStateNames[] = { "DNG", "VUL" };

        sprintf(buf, "ENM A:%d ST:%s X:%d Y:%d",
                enemy.alive, enemyStateNames[enemy.state], enemy.x, enemy.y);
        VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW + 5);
    }

    sprintf(buf, "ENM STMR:%d DTMR:%d GONE:%d", enemy.stateTimer, enemy.deathTimer, Enemy_isGone(&enemy));
    VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW + 6);

    sprintf(buf, "ITM %d/%d ALL:%d", Items_collectedCount(), itemCount, Items_allCollected());
    VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW + 7);

    sprintf(buf, "MSEED:%u RSEED:%u", mapSeed, roomSeedFor(currentCol, currentRow));
    VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW + 8);

    sprintf(buf, "SHK F:%d A:%d TMBMV:%d TIDX:%d", shakeFramesLeft, shakeAmplitude, tombMoving, tombSpeedIdx);
    VDP_drawTextBG(BG_B, buf, 1, DEBUG_VIEW_FIRST_ROW + 9);

    VDP_setTextPriority(0);
}

// Starts a run on the selected planet. The caller has ALREADY faded to
// the backdrop -- both entry points do, and one of them (the cartography
// screen handing over) is already black by the time it gets here.
static void startSelectedPlanet(void)
{
    Menu_hideSprites();
    flushSprites();    // no planets left in the table BEFORE their palette changes hands
    gameState = STATE_PLAYING;
    newGame();
    // The ship waits outside until the room is all the way in
    // (user request: "quiero que la nave no aparezca en
    // pantalla entrando hacia la insertion room hasta que el
    // fundido haya terminado"). newGame() put it on screen;
    // take it back off, push that, and only show it once the
    // fade is done -- the main loop's own SPR_update, at the
    // end of this very frame, is what puts it there.
    SPR_setVisibility(playerSprite, HIDDEN);
    flushSprites();
    // Dead last, with nothing between it and the fade:
    // Menu_setVisible puts PAL0's index1 back to the maze's own
    // wall colour, and newGame() above drew a whole room in it.
    // Anything that waits for a vblank after this -- and
    // flushSprites does -- is a frame of that room on screen at
    // full strength. fadeFromBackdrop flattens CRAM as its
    // first act, so from here there is no vblank in between.
    Menu_setVisible(FALSE);
    fadeFromBackdrop();
    SPR_setVisibility(playerSprite, VISIBLE);
}

int main(bool hardReset)
{
    u16 prevState = 0;

    VDP_setPlaneSize(64, 32, TRUE);
    // Our own font, over the one SGDK loaded during its own VDP init:
    // same 96 tiles at the same TILE_FONT_INDEX, so every VDP_drawText in
    // the game picks it up with nothing else to change. Must happen
    // before anything draws text.
    VDP_loadFont(&gameFont, DMA);

    SPR_init();
    initPlantCacheSlots(); // before any cartography pass fills the cache

    Maze_loadGraphics();
    GuideMap_loadGraphics();
    Menu_loadGraphics(); // spec §31 -- must come after both above, its tiles stack right after theirs in VRAM
    Sfx_loadDriver(); // spec §47/§49 -- PCM4 driver for the kick and plant hi-hat, independent of VRAM layout

    PAL_setPalette(PAL1, playerShip.palette->data, DMA);
    PAL_setColor(PLAYER_SHIP_INK_INDEX, PLAYER_SHIP_COLOR);
    playerSprite = SPR_addSprite(&playerShip, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    // mapShip (spec §24): same PAL1, same 2 colors as playerShip, so no
    // separate palette load -- just a smaller (1 tile, 8x8) silhouette
    // shown instead of playerShip while the guide map is open.
    mapShipSprite = SPR_addSprite(&mapShip, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));

    // PAL2 is enemyShip's own palette (also reused by the menu's
    // "completed planet" yellow -- ENEMY_INK_INDEX's own doc comment).
    PAL_setPalette(PAL2, enemyShip.palette->data, DMA);
    enemySprite = SPR_addSprite(&enemyShip, 0, 0, TILE_ATTR(PAL2, TRUE, FALSE, FALSE));

    JOY_init();

    // STATE_INTRO (user request, see the enum's own doc comment) --
    // Menu_setVisible/drawMenu() are deferred to the main loop's
    // STATE_INTRO dispatch below, once introBegin()/introTick() (one
    // room per frame) have worked through every planet.
    gameState = STATE_INTRO;
    SPR_setVisibility(playerSprite, HIDDEN);
    SPR_setVisibility(mapShipSprite, HIDDEN);
    SPR_setVisibility(enemySprite, HIDDEN);
    drawIntroPrompt();

    while (TRUE)
    {
        const u16 state = JOY_readJoypad(JOY_1);

        updateDebugCombo(state, prevState); // checked every frame, any gameState (see its own doc comment)

        if (((state & RESET_COMBO) == RESET_COMBO) && ((prevState & RESET_COMBO) != RESET_COMBO))
        {
            resetToMenu();
        }
        else if (gameState == STATE_INTRO)
        {
            if (!introGenerating)
            {
                // Any real button -- same "first press reseeds random()"
                // moment spec §54 already relies on elsewhere, which is
                // exactly why this screen exists (introBegin/introTick's
                // own doc comment): introBegin() below draws on that
                // safely.
                if ((state & (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START)) &&
                    !(prevState & (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START)))
                {
                    introBegin();
                    introGenerating = TRUE;
                }
            }
            else
            {
                introFloodStep(); // one glyph per frame, every frame -- see its own doc comment
                if (introTick()) // one room per frame -- see its own doc comment
                {
                    fadeToBackdrop(); // whatever comes next is built behind the black
                    VDP_clearPlane(BG_B, TRUE); // BG_B is HUD/debug territory during real play
                    invalidateHudLines();
                    introGenerating = FALSE;

                    if (cartographyThenPlay)
                    {
                        // Pressing A brought us here: go straight into the
                        // run the player already committed to, never back
                        // to the menu they already left.
                        cartographyThenPlay = FALSE;
                        startSelectedPlanet();
                    }
                    else
                    {
                        gameState = STATE_MENU;
                        Menu_setVisible(TRUE);
                        Menu_hideSprites(); // same as resetToMenu: the sprites fade in after the screen
                        drawMenu();
                        fadeFromBackdrop();
                        {
                            bool completed[SIZE_PRESET_COUNT];

                            planetCompletedFlags(completed);
                            Menu_fadeInSprites(sizePresetIndex, unlockedPlanets, completed);
                        }
                    }
                }
            }
        }
        else if (gameState == STATE_SCORES)
        {
            // Any button at all goes back (user request: "si el usuario
            // pulsa boton en high scores se va al menu de nuevo"). Edge
            // detected, so the press that brought us here cannot bounce
            // straight back out, and the same press is not seen by the
            // menu either -- prevState carries it into next frame.
            const u16 anyButton = BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START |
                                  BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT;

            if ((state & anyButton) && !(prevState & anyButton))
                leaveScoreTable();
            else if (++screenAlternateTimer >= SCREEN_ALTERNATE_FRAMES)
                leaveScoreTable(); // its turn is over, hand back to the menu
        }
        else if (gameState == STATE_MENU)
        {
            // Cursor arrow tracks the selected planet's live orbit
            // position (spec §31) -- advanced every frame regardless of
            // input. completed[] (spec §45, user request: "Los
            // planetas que se han completados pintalos de amarillo")
            // is derived fresh each frame from presetSave[] -- cheap
            // (SIZE_PRESET_COUNT checks) and avoids needing to hook into
            // every place presetSave[] can change.
            bool completed[SIZE_PRESET_COUNT];

            planetCompletedFlags(completed);

            // Checked every frame, not just on a run ending: a planet
            // that was blocking the next unlock by being uncharted stops
            // blocking it the moment the cartography screen maps it, and
            // this is what notices. A planet opening changes what the menu
            // says about the one under the cursor, so redraw when it does.
            {
                const u8 before = unlockedPlanets;

                updatePlanetUnlocks();
                if (unlockedPlanets != before)
                    drawMenu();
            }

            Menu_update(sizePresetIndex, unlockedPlanets, completed);

            // Every planet can be looked at, locked or not (user request:
            // "planetas bloqueados son seleccionables, pero te indica que
            // objetos necesitan ser [desbloqueados] para entrar") -- what
            // a locked one refuses is A, down below, not the cursor.
            if ((state & BUTTON_LEFT) && !(prevState & BUTTON_LEFT))
            {
                sizePresetIndex = (u8) (DEBUG_FIRST_PLANET + ((sizePresetIndex - DEBUG_FIRST_PLANET + PLANET_SEL_COUNT - 1) % PLANET_SEL_COUNT));
                drawMenu();
            }
            if ((state & BUTTON_RIGHT) && !(prevState & BUTTON_RIGHT))
            {
                sizePresetIndex = (u8) (DEBUG_FIRST_PLANET + ((sizePresetIndex - DEBUG_FIRST_PLANET + 1) % PLANET_SEL_COUNT));
                drawMenu();
            }
            if ((state & BUTTON_A) && !(prevState & BUTTON_A) && (sizePresetIndex < unlockedPlanets))
            {
                // Map it now if the boot screen did not (user request:
                // "quiero que cargue el planeta cuando se pulsa A para
                // entrar, no cuando se selecciona"). Browsing the bar
                // stays instant; the wait lands on the one planet the
                // player actually committed to, and only the first time.
                fadeToBackdrop();  // the whole run is set up below, behind the flat screen
                if (!presetSave[sizePresetIndex].plantsTotalKnown)
                {
                    Menu_hideSprites();
                    flushSprites();
                    Menu_setVisible(FALSE);
                    gameState = STATE_INTRO;
                    introGenerating = TRUE;
                    cartographyThenPlay = TRUE; // its completion starts the run instead of returning to the menu
                    beginCartography(sizePresetIndex, (u8) (sizePresetIndex + 1), FALSE);
                    fadeFromBackdrop();
                }
                else
                    startSelectedPlanet();
            }
            // Sound toggle (spec §47, user request: "activable desde el
            // menu y por defecto apagado") -- BUTTON_C alone, free in the
            // menu (only ever meaningful as part of the 3-button
            // RESET_COMBO/DEBUG_VIEW_COMBO chords
            // above, which a lone C press never satisfies).
            if ((state & BUTTON_C) && !(prevState & BUTTON_C))
            {
                Sfx_setEnabled(!Sfx_isEnabled());
                drawMenu();
            }

            // Hand over to the high-score table after a while untouched.
            // Guarded on still being in the menu: pressing A above starts
            // a run, and this must not then pull the table over it. Any
            // input at all restarts the wait, so the table never appears
            // while the planet bar is being browsed.
            if (gameState == STATE_MENU)
            {
                if (state != prevState)
                    screenAlternateTimer = 0;
                else if (++screenAlternateTimer >= SCREEN_ALTERNATE_FRAMES)
                    enterScoreTable();
            }
        }
        else if (gameState == STATE_GAMEOVER) // user request, see its own enum doc comment
        {
            if ((state & BUTTON_A) && !(prevState & BUTTON_A))
                resetToMenu();
        }
        // Both map-screen tools (only ever on during STATE_PLAYING) run
        // INSTEAD of the game: the ship, the enemy and every collection
        // check are frozen for as long as one owns the screen, the same
        // way the map overlay already freezes them.
        else if (roomViewerOn)
            updateRoomViewer(state, prevState);
        else if (sweepOn)
            updateSweep(state, prevState);
        else // STATE_PLAYING
        {
            // moveSpeed is computed ONCE per frame here and read by BOTH
            // Player_updateRoom call sites below (one for the insertion room,
            // one for a real room -- only one of those branches ever runs a
            // given frame, but both need it).
            u8 moveSpeed;
            const s16 frameStartX = player.x;
            const s16 frameStartY = player.y;

            // B: the route to the nearest plant, shown for exactly as long
            // as B is HELD (user request) -- the same "hold it up and look
            // through it" rule the map device on A already has, no on/off
            // memory either way. Nothing of PLANTPATH_EXCLUDE may be down
            // with it, so it can't come on as part of a debug chord or
            // while the map (A) is up, where B belongs to the map instead.
            {
                const bool wantRoute = ((state & PLANTPATH_BUTTON) != 0) && !(state & PLANTPATH_EXCLUDE);

                if (wantRoute != plantPathOn)
                {
                    plantPathOn = wantRoute;
                    if (plantPathOn)
                        plantPathDirty = TRUE;  // force a first search, whatever cell the ship is on
                    else
                        clearPlantPathMarks();  // drawPlantPath() will not run again to do it itself
                }
            }

            // Each D-pad direction sets the ship's facing directly --
            // edge-triggered (a NEW press, not held), so once set it keeps
            // going until a fresh press of a DIFFERENT direction redirects
            // it, or a wall stops it in place. Priority order when more than
            // one is newly pressed the same frame (no diagonals): UP, DOWN,
            // LEFT, RIGHT.
            {
                u8 pressed = DIR_NONE;

                if ((state & BUTTON_UP) && !(prevState & BUTTON_UP)) pressed = DIR_UP;
                else if ((state & BUTTON_DOWN) && !(prevState & BUTTON_DOWN)) pressed = DIR_DOWN;
                else if ((state & BUTTON_LEFT) && !(prevState & BUTTON_LEFT)) pressed = DIR_LEFT;
                else if ((state & BUTTON_RIGHT) && !(prevState & BUTTON_RIGHT)) pressed = DIR_RIGHT;

                if (tombMoving)
                {
                    // Mid-slide: can't steer, but remember the latest press.
                    if (pressed != DIR_NONE)
                        tombQueuedDir = pressed;
                }
                else
                {
                    // At a stop: a fresh press wins over a queued one.
                    if (pressed != DIR_NONE)
                        player.dir = pressed;
                    else if (tombQueuedDir != DIR_NONE)
                        player.dir = tombQueuedDir;
                    tombQueuedDir = DIR_NONE;
                }
            }

            // "Tomb of the Mask" (spec §44): one press slides the ship all
            // the way to the next wall or door -- stops there, no bouncing.
            // Ease-in: the first frame of a slide (the ship was at a stop,
            // tombMoving FALSE) is tombRamp[0]; every frame it keeps
            // travelling steps one further up the ramp. A stop sends it back
            // to the start, and a room change mid-slide just carries on (the
            // ship still counts as moving).
            if ((player.dir == DIR_NONE) || !tombMoving)
                tombSpeedIdx = 0;
            else if (tombSpeedIdx < TOMB_RAMP_LAST)
                tombSpeedIdx++;
            moveSpeed = (player.dir == DIR_NONE) ? 0 : tombRamp[tombSpeedIdx];

            // Mirror for the debug view (drawDebugView) -- moveSpeed above is
            // a local to this block, not reachable from the top-level draw call.
            moveSpeedDebug = moveSpeed;

            // Map view disabled while still in the insertion room (spec
            // §27) -- currentCol/currentRow aren't set yet, and there's
            // nothing to preview before the player has even entered the
            // grid.
            // HELD, not toggled (user request: "el mapa tampoco se activa,
            // si no que se muestra mientras A esta pulsado" -- the real
            // map used to be a press-to-open/press-to-close toggle; now it
            // follows the exact same "shows for as long as A stays down"
            // rule the static noise already had, so both halves of this
            // device behave identically). Which of the two actually shows
            // depends on whether letter A -- the in-fiction "map device"
            // -- has been collected yet (Items_collectedCount(), see
            // mapShowingNoise's own doc comment): static beforehand, the
            // real overlay afterwards. Either way it vanishes the instant
            // A is released, no on/off memory.
            // The map works from the insertion room too (user request:
            // "desde la sala de insercion el mapa (si ya lo tiene la nave
            // por que ha recogido A en ese planeta) tiene que estar
            // disponible") -- same rules as anywhere else, the real
            // overlay once letter A is in hand and the static until then.
            // The room is drawn on the map already; what it never had was
            // a way to open it from inside.
            {
                if (state & BUTTON_A)
                {
                    if (!mapViewOpen)
                    {
                        mapViewOpen = TRUE;
                        mapShowingNoise = !(Items_collectedCount() > 0);

                        if (mapShowingNoise)
                        {
                            // The static is "this device has no signal" --
                            // say so (user request), on BG_B so the noise,
                            // which repaints all of BG_A every frame,
                            // can't swallow it. Centred on the row the
                            // static now grows out from.
                            // White letters in their own black box (user
                            // request). The letters are BG_B at high
                            // priority, drawn once here; the box is a
                            // rectangle of maze.c's solid tile on BG_A,
                            // repainted under them every frame because the
                            // static owns that whole plane.
                            VDP_setTextPriority(1);
                            VDP_drawTextBG(BG_B, NO_MAP_SIGNAL_TEXT,
                                            (u16) ((40 - NO_MAP_SIGNAL_LEN) / 2), NO_MAP_SIGNAL_ROW);
                            VDP_setTextPriority(0);

                            // No map device yet -- static instead of a
                            // real map, same PAL1-white-ink/PSG-noise-
                            // channel pair Maze_drawNoiseFrame's own doc
                            // comment describes. Nothing marks the current
                            // room (there's no mapShip blink for a screen
                            // with no signal), so just hide everything
                            // room-related.
                            SPR_setVisibility(playerSprite, HIDDEN);
                            SPR_setVisibility(mapShipSprite, HIDDEN);
                            SPR_setVisibility(enemySprite, HIDDEN);
                            PAL_setColor(PLAYER_SHIP_INK_INDEX, RGB24_TO_VDPCOLOR(0xFFFFFF));
                            PSG_setNoise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK2);
                            PSG_setEnvelope(3, 0); // 0 = loudest (PSG_ENVELOPE_MAX)
                        }
                        else
                        {
                            u16 mx, my;

                            GuideMap_drawOverlay();

                            // mapShip (spec §24, 8x8, same size as a map
                            // letter) marks the current room instead of
                            // playerSprite (which hides) -- centered in
                            // the 24x16px room box: (24-8)/2=8
                            // horizontally, (16-8)/2=4 vertically. White
                            // and blinking (spec §23): flip the shared ink
                            // color, start visible, reset the blink timer
                            // -- the per-frame toggle below picks it up
                            // from here.
                            if (inInsertRoom)
                                GuideMap_insertRoomBoxPixelPos(&mx, &my);
                            else
                                GuideMap_roomBoxPixelPos(currentCol, currentRow, &mx, &my);
                            SPR_setPosition(mapShipSprite, mx + 8, my + 4);
                            PAL_setColor(PLAYER_SHIP_INK_INDEX, RGB24_TO_VDPCOLOR(0xFFFFFF));
                            SPR_setVisibility(playerSprite, HIDDEN);
                            SPR_setVisibility(mapShipSprite, VISIBLE);
                            SPR_setVisibility(enemySprite, HIDDEN); // frozen with everything else while the map is open
                            mapBlinkTimer = 0;
                        }
                    }

                    // What the map screen's own buttons do, with A still
                    // held (user request, moved here from an earlier
                    // layout where the tools were on a lone B/C and the
                    // reveal on a lone START):
                    //   B           -- lift the fog, show every room
                    //   START + B   -- the room viewer
                    //   START + C   -- the route sweep
                    // Nothing here can be confused with a global debug
                    // chord: every one of those either needs a direction,
                    // or (B+C / B+START) excludes A, which is held for as
                    // long as this screen is up. B+C+START (HITBOX) is the
                    // one with no exclude mask of its own, and it needs B
                    // and C together, which neither of these pairs is.
                    //
                    // START is read as HELD, not as a press of its own, so
                    // it is the modifier and B/C stay edge-triggered.
                    if ((state & BUTTON_START) && !inInsertRoom)
                    {
                        // Both tools take the screen over and shut the map
                        // down themselves, so releasing A afterwards
                        // doesn't paint over them. Offered over the static
                        // too, not just over the real map: a debug tool has
                        // no business waiting on the player to find letter
                        // A first.
                        if ((state & BUTTON_B) && !(prevState & BUTTON_B))
                            enterRoomViewer();
                        else if ((state & BUTTON_C) && !(prevState & BUTTON_C))
                            enterSweep();
                        else if (mapShowingNoise)
                        {
                            Maze_drawNoiseFrame();
                            Maze_drawSolidBox(NO_MAP_SIGNAL_BOX_X, NO_MAP_SIGNAL_ROW - 1,
                                               NO_MAP_SIGNAL_LEN + 2, 3);
                        }
                    }
                    // B alone: lift the fog (user request, "que el mapa
                    // enseñe todas las habitaciones disponibles en el mapa
                    // sin ocultarlas"). Only over the real overlay -- the
                    // static is the screen for having no map device at all,
                    // there is nothing drawn there to reveal. The flag
                    // lives in guidemap.c and stays put, so the map comes
                    // back revealed next time A is held, until B turns it
                    // off again.
                    else if (!mapShowingNoise && (state & BUTTON_B) && !(prevState & BUTTON_B))
                    {
                        GuideMap_setRevealAll(!GuideMap_revealAll());
                        GuideMap_drawOverlay();
                    }
                    else if (mapShowingNoise)
                    {
                        Maze_drawNoiseFrame(); // fresh static every frame it's held
                        Maze_drawSolidBox(NO_MAP_SIGNAL_BOX_X, NO_MAP_SIGNAL_ROW - 1,
                                           NO_MAP_SIGNAL_LEN + 2, 3); // the banner's own backdrop
                    }
                }
                else if (mapViewOpen) // A just released -- close whichever was open
                {
                    if (mapShowingNoise)
                    {
                        PSG_setEnvelope(3, PSG_ENVELOPE_MIN); // silence
                        VDP_clearTextLineBG(BG_B, NO_MAP_SIGNAL_ROW); // the banner above
                    }

                    Maze_draw();
                    // Maze_draw() repaints every tile of BG_A, wiping
                    // whatever the room had on it along with the map
                    // overlay (or the static) -- put it back. The
                    // insertion room has no letter and no plants of its
                    // own, just its door arrow (spec §37), and
                    // currentCol/currentRow are stale while in there.
                    if (inInsertRoom)
                    {
                        drawInsertRoomArrow();
                    }
                    else
                    {
                        Items_drawInRoom(currentCol, currentRow);
                        Plants_drawInRoom(currentCol, currentRow);
                    }
                    // Restores the ship's normal color (spec §23) -- only
                    // the one word that PLAYER_SHIP_INK_INDEX touched, the
                    // transparent index0 was never changed.
                    PAL_setColor(PLAYER_SHIP_INK_INDEX, PLAYER_SHIP_COLOR);
                    SPR_setVisibility(playerSprite, VISIBLE);
                    SPR_setVisibility(mapShipSprite, HIDDEN);
                    // No enemy in the insertion room (spec §27) -- `enemy`
                    // is still whatever the last real room left in it, so
                    // asking it whether to show the sprite would conjure
                    // one up in a room that never had one.
                    SPR_setVisibility(enemySprite,
                                       (!inInsertRoom && Enemy_blinkVisible(&enemy)) ? VISIBLE : HIDDEN);
                    mapViewOpen = FALSE;
                    mapShowingNoise = FALSE;
                }
            }

            if (mapViewOpen || roomViewerOn || sweepOn)
            {
                // Either map-screen tool freezes the game exactly like the
                // overlay does -- and it is switched on from inside the
                // block just above, mid-frame, so without naming them here
                // the rest of THIS frame would still play a step of the
                // game underneath the tool's own screen.
                //
                // Static (mapShowingNoise) already got a fresh
                // Maze_drawNoiseFrame() call above, for as long as A stays
                // held -- nothing more to do for it here. The real map's
                // own mapShip blink (spec §23/§24): flip visibility every
                // MAP_BLINK_FRAMES frames while the overlay stays open.
                // Position doesn't need re-setting each frame -- it's
                // static while viewing the map.
                if (!mapShowingNoise)
                {
                    mapBlinkTimer++;
                    if (mapBlinkTimer >= MAP_BLINK_FRAMES)
                    {
                        mapBlinkTimer = 0;
                        SPR_setVisibility(mapShipSprite, SPR_isVisible(mapShipSprite, FALSE) ? HIDDEN : VISIBLE);
                    }
                }
            }
            else if (inInsertRoom)
            {
                // The insertion room has two doors (spec §27/§36):
                // insertRoomDoorDir (the mission door, spec §29ter) and
                // menuDoorDir (perpendicular to it by construction, so
                // never the same side) -- no items/locks apply
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
                const s16 preShakeX = player.x, preShakeY = player.y;
                const u8 exitDir = Player_updateRoom(&player, moveSpeed,
                                                      doorN, doorE, doorS, doorW, offN, offE, offS, offW);

                // Screenshake on a wall hit (user request), edge-triggered
                // (see wasWallBlocked's own doc comment) -- same check as
                // the normal-room branch's own sub-step loop, just against
                // this single (possibly multi-px) call's start/end position
                // instead, since this room has no per-substep letter/enemy
                // work needing that finer granularity.
                {
                    const bool blockedNow = (exitDir == EXIT_NONE) && (player.dir != DIR_NONE) &&
                                             (player.x == preShakeX) && (player.y == preShakeY);

                    // No per-substep granularity here (this room calls
                    // Player_updateRoom once with the full moveSpeed, see
                    // above) -- the whole frame's worth of travel is added
                    // at once instead of 1px at a time.
                    if (blockedNow)
                    {
                        if (!wasWallBlocked)
                        {
                            triggerShake(slideDistance);
                            triggerSquash(player.dir); // the wall it hit head-on is the way it was going
                            Sfx_playWallHit(slideDistance); // spec §47, user request -- volume proportional to distance, like the shake
                        }
                        slideDistance = 0;
                    }
                    else
                    {
                        const s16 moved = (s16) (abs(player.x - preShakeX) + abs(player.y - preShakeY));

                        if (moved > 0)
                            slideDistance = (u16) (slideDistance + moved);
                    }
                    wasWallBlocked = blockedNow;
                }

                if (exitDir == exitDirForDoorDir(insertRoomDoorDir))
                {
                    // Jump straight to the chosen perimeter room and land
                    // right at its real border opening (spec §29,
                    // insertLinkDir -- loadRoom() already punched it
                    // open, same call as for any of that room's own tree
                    // doors), aligned with that door's actual span (spec
                    // §30).
                    inInsertRoom = FALSE;
                    currentCol = insertLinkCol;
                    currentRow = insertLinkRow;
                    latchDoorOpen(currentCol, currentRow, insertLinkDir); // crossed for real -- open from here on
                    loadRoom(currentCol, currentRow);
                    positionPlayerEnteringViaDoorDir(insertLinkDir, insertLinkOffset);
                    drawInsertRoomStatus(FALSE); // spec §34 -- only relevant while actually in the insertion room

                }
                else if (exitDir == exitDirForDoorDir(menuDoorDir))
                {
                    // Walked out through the menu-exit door (spec §36),
                    // whether every letter is collected (phase complete)
                    // or not -- straight back to the menu either way.
                    // resetToMenu() saves progress (spec §35);
                    // collectedCount naturally equals itemCount when the
                    // run was won, which is exactly what should show
                    // there (its own doc comment). Used to show a "FASE
                    // COMPLETADA" screen first and wait for BUTTON_A when
                    // Items_allCollected() -- removed per user request
                    // ("elimina la pantalla de fase completada, simplemente
                    // vuelve al menu").
                    resetToMenu();
                }

                SPR_setPosition(playerSprite, player.x + Maze_originPxX(), player.y + Maze_originPxY()); // room pixels -> screen: HUD band plus the room's own centring (maze.h)
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
                const u8 offN = doorOffsetFor(currentCol, currentRow, DOOR_N);
                const u8 offE = doorOffsetFor(currentCol, currentRow, DOOR_E);
                const u8 offS = doorOffsetFor(currentCol, currentRow, DOOR_S);
                const u8 offW = doorOffsetFor(currentCol, currentRow, DOOR_W);
                u8 exitDir = EXIT_NONE;
                bool playerDied = FALSE;
                u8 step;

                // Enemy moves once per visual frame (its own placeholder
                // pace, see enemy.c), BEFORE the ship's own sub-step loop
                // below -- so every one of this frame's collision checks
                // sees the same, already-advanced enemy position. Its ink
                // color is set every frame too (cheap, one PAL_setColor)
                // rather than only on a state change, simplest way to keep
                // it in sync with enemy.state.
                Enemy_update(&enemy);
                PAL_setColor(ENEMY_INK_INDEX, (enemy.state == ENEMY_DANGEROUS) ? ENEMY_DANGEROUS_COLOR : ENEMY_VULNERABLE_COLOR);
                SPR_setPosition(enemySprite, enemy.x + Maze_originPxX(), enemy.y + Maze_originPxY());
                SPR_setVisibility(enemySprite, Enemy_blinkVisible(&enemy) ? VISIBLE : HIDDEN);

                // One 1px sub-step at a time, checking for the letter (and
                // the enemy) after EACH one instead of once per frame. A
                // frame can move the ship several px, and a per-frame
                // overlap check only ever saw its resting cell for that
                // frame -- it could step clean over the letter (the hub,
                // which is exactly where tombo puts it) or the enemy.
                // Intermediate pixels are what count.
                for (step = 0; (step < moveSpeed) && (exitDir == EXIT_NONE) && !playerDied; step++)
                {
                    const s16 preShakeX = player.x, preShakeY = player.y;

                    exitDir = Player_updateRoom(&player, 1, doorN, doorE, doorS, doorW,
                                                offN, offE, offS, offW);

                    // Screenshake on a wall hit (user request), edge-
                    // triggered (see wasWallBlocked's own doc comment) --
                    // the ship tried to move (a real direction is set) but
                    // didn't, true whether it just stopped (NORMAL/INERTIA/
                    // TOMB).
                    {
                        const bool blockedNow = (exitDir == EXIT_NONE) && (player.dir != DIR_NONE) &&
                                                 (player.x == preShakeX) && (player.y == preShakeY);

                        if (blockedNow)
                        {
                            if (!wasWallBlocked)
                            {
                                triggerShake(slideDistance);
                                triggerSquash(player.dir); // the wall it hit head-on is the way it was going
                                Sfx_playWallHit(slideDistance); // spec §47, user request -- volume proportional to distance, like the shake
                            }
                            slideDistance = 0; // this slide just ended, next one starts fresh
                        }
                        else if ((player.x != preShakeX) || (player.y != preShakeY))
                        {
                            slideDistance++; // still sliding -- one more px of run-up
                        }
                        wasWallBlocked = blockedNow;
                    }

                    // Cheap box pre-check first: this runs once per px moved,
                    // and the letter only lives in the hub's cell.
                    if ((exitDir == EXIT_NONE) &&
                        (player.x > (MAZE_DOOR_COL - 1) * MAZE_TILE_PX) && (player.x < (MAZE_DOOR_COL + 1) * MAZE_TILE_PX) &&
                        (player.y > (MAZE_DOOR_ROW - 1) * MAZE_TILE_PX) && (player.y < (MAZE_DOOR_ROW + 1) * MAZE_TILE_PX) &&
                        Items_tryCollect(currentCol, currentRow, player.x, player.y))
                    {
                        Sfx_playLetterPickup(); // user request -- placeholder, see sfx.h
                        plantPathDirty = TRUE;  // the route may have been pointing at exactly this letter

                        // Unlocks the next branch (spec §16) -- affects
                        // rooms not yet loaded, they pick up the new lock
                        // state next time loadRoom() regenerates them.
                        // Checked BEFORE roomFullyCleared() below in case
                        // it ever mattered for this room's own neighbors
                        // too (it normally doesn't -- a letter's room is
                        // always a dead end -- but costs nothing to order
                        // this way regardless).
                        GuideMap_recomputeLocks();

                        if (roomFullyCleared(currentCol, currentRow))
                        {
                            // This room's own doors may have just gone
                            // from sealed to open (user request: "las
                            // puertas se abren en cada habitación siempre
                            // que se hayan recogido las plantas y la
                            // letra de la misma") -- regenerates+redraws
                            // (wipes the now-collected letter's tile too).
                            applyRoomDoorLocks(currentCol, currentRow);
                        }
                        else
                        {
                            Maze_draw();           // wipes the now-collected letter's tile (and any plants, spec §48)
                            Plants_drawInRoom(currentCol, currentRow);
                        }
                        Items_drawHud();
                    }

                    // Plant pickup (spec §48, user request) -- no cheap
                    // box pre-check like the letter's above: plants can
                    // sit anywhere in the room, not just the hub cell, but
                    // Plants_tryCollect is only ever a handful of AABB
                    // tests (PLANTS_MAX_PER_ROOM=5), cheap enough to just
                    // call every sub-step unconditionally.
                    if ((exitDir == EXIT_NONE) && Plants_tryCollect(currentCol, currentRow, player.x, player.y))
                    {
                        if (roomFullyCleared(currentCol, currentRow))
                        {
                            // This room's own doors may have just opened
                            // (same user request as the letter pickup
                            // above) -- regenerates+redraws (wipes the
                            // now-collected plant's tile too).
                            applyRoomDoorLocks(currentCol, currentRow);
                        }
                        else
                        {
                            Maze_draw();           // wipes the now-collected plant's tile (and the letter, if still uncollected)
                            Items_drawInRoom(currentCol, currentRow);
                            Plants_drawInRoom(currentCol, currentRow);
                        }
                        Plants_drawHud();
                        Sfx_playPlantPickup(); // user request: hi-hat cerrado de la 909 al recoger una planta
                        plantPathDirty = TRUE; // the route may have been pointing at exactly this one
                    }

                    // Enemy contact (user request): ENEMY_DANGEROUS kills
                    // the player outright (STATE_GAMEOVER, handled right
                    // after this loop); ENEMY_VULNERABLE is the other way
                    // around -- running it over ("atropellarlo... como si
                    // fuese una melee") kills IT instead, via Enemy_kill's
                    // own placeholder death blink.
                    if ((exitDir == EXIT_NONE) && enemy.alive && Enemy_overlapsBox(&enemy, player.x, player.y))
                    {
                        triggerShake(slideDistance); // user request: shake on hitting an enemy too, either outcome

                        if (enemy.state == ENEMY_DANGEROUS)
                            playerDied = TRUE;
                        else
                        {
                            Enemy_kill(&enemy);
                            Sfx_playEnemyKill(); // user request -- placeholder, see sfx.h
                            // Remember it for good (user request: "los
                            // enemigos no reaparecen") -- loadRoom() checks
                            // this on every future visit to this room.
                            GuideMap_markEnemyDead(currentCol, currentRow);
                            // Bounce (user request: "cuando haga melee...
                            // y lo mate, la nave rebota") -- reverses
                            // p->dir on the spot (UP<->DOWN, LEFT<->RIGHT),
                            // same "+2 mod 4" opposite every other pairing
                            // in this codebase uses. Takes effect on the
                            // very next sub-step, still this same frame:
                            // the ship recoils away from where the enemy
                            // was instead of sliding on through it.
                            player.dir = (u8) ((player.dir + 2) & 3);
                        }
                    }
                }

                if (playerDied)
                {
                    // STATE_GAMEOVER (user request): "hold the state
                    // until BUTTON_A" -- see its own enum doc comment for
                    // why resetToMenu() below won't save this attempt's
                    // progress.
                    gameState = STATE_GAMEOVER;

                    SPR_setVisibility(playerSprite, HIDDEN);
                    SPR_setVisibility(enemySprite, HIDDEN);
                    drawInsertRoomStatus(FALSE);

                    VDP_clearPlane(BG_A, TRUE);
                    VDP_drawText("GAME OVER", 15, 12);
                    VDP_drawText("PULSA A PARA VOLVER AL MENU", 6, 15);
                }
                else if (exitDir != EXIT_NONE)
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
                        inInsertRoom = TRUE;
                        Maze_generateInsertionRoom(insertRoomDoorDir, insertLinkOffset, menuDoorDir, menuDoorOffset, mapSeed);
                        Maze_draw();
                        drawInsertRoomArrow(); // spec §37
                        positionPlayerEnteringViaDoorDir(insertRoomDoorDir, insertLinkOffset);
                        drawInsertRoomStatus(TRUE); // spec §34
                        SPR_setVisibility(enemySprite, HIDDEN); // no enemy in the insertion room (spec §27)
                    }
                    else
                    {
                        enterRoomFrom(exitDir);
                    }
                }

                SPR_setPosition(playerSprite, player.x + Maze_originPxX(), player.y + Maze_originPxY()); // room pixels -> screen: HUD band plus the room's own centring (maze.h)
            }

            // Did the ship travel this frame? (The steering lock,
            // see tombMoving.) Frozen while the map is open: nothing moves.
            if (!mapViewOpen)
            {
                const bool wasMoving = tombMoving;

                tombMoving = (player.x != frameStartX) || (player.y != frameStartY);

                // Score (user request). A MOVE is the start of a slide --
                // the FALSE->TRUE edge -- not a button press: steering into
                // a wall never moves the ship and must not cost anything.
                // Both counters stop for good once the planet's objectives
                // are met, which is what makes the score a measure of the
                // run rather than of how long the pad was left idle.
                if (!presetSave[sizePresetIndex].scoreDone)
                {
                    if (tombMoving && !wasMoving)
                        presetSave[sizePresetIndex].scoreMoves++;

                    presetSave[sizePresetIndex].scoreFrames++;

                    if (Items_allCollected() &&
                        (Plants_collectedCount() >= presetSave[sizePresetIndex].plantsTotal) &&
                        allEnemiesDead())
                        presetSave[sizePresetIndex].scoreDone = TRUE;
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
            // SYS_getFPS() still runs every frame (its own doc comment
            // requires exactly one call per frame); only the sprintf and
            // the glyph poking are skipped while the number is unchanged,
            // which it is for ~59 frames out of 60.
            const u32 fps = SYS_getFPS();
            static u32 prevFps = 0xFFFFFFFF;

            // The score owns the top-right corner now (user request), so
            // the FPS readout moves left of it rather than over it. Its
            // own slot is a fixed width so the two never collide whatever
            // either number does.
            if (!debugInfoOn)
                prevFps = 0xFFFFFFFF; // so it redraws the moment DATOS comes back on
            else if ((fps != prevFps) || hudLinesDirty)
            {
                char buf[8];
                int len = sprintf(buf, "FPS%lu", fps);

                prevFps = fps;
                VDP_setTextPriority(1);
                VDP_drawTextBG(BG_B, buf, (u16) (SCORE_COL - len - 1), 0);
                VDP_setTextPriority(0);
            }
        }

        drawScoreHud();
        updateSquash(); // the ship's own deformation, see triggerSquash

        drawRoomIdHud(); // always-on room identity line, STATE_PLAYING only
        hudLinesDirty = FALSE; // both readers above have had their frame
        drawDebugView(); // no-op unless the debug combo turned it on
        drawCollisionGizmos(); // no-op unless HITBOX is on and gameState is STATE_PLAYING
        drawPlantPath(); // no-op unless PLANTA is on and gameState is STATE_PLAYING
        drawReturnToInsertHint(); // no-op until every letter is in hand

        prevState = state;

        updateShake();
        SPR_update();

        SYS_doVBlankProcess();
    }

    return 0;
}
