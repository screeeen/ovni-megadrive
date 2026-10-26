#include "enemy.h"
#include "maze.h"
#include "player.h"
#include "plants.h"

// El borde real de la caja de una celda, igual que player.c: asi un enemigo
// se para justo sobre la rejilla y no un pixel mas alla.
#define BOX  (MAZE_TILE_PX - 1)
#define TRAJ (MAZE_TILE_PX - 1)

#define EMOVE_LANE  0
#define EMOVE_STILL 1
#define EMOVE_RING  2
#define EMOVE_STALK 3
#define EMOVE_SLIDE 4
#define EMOVE_GHOST 5

// Fotogramas parado entre dos deslizamientos (ESP_DESLIZA), y cada cuanto
// se reorienta el espectro.
#define SLIDE_PAUSE_FRAMES 36
#define GHOST_AIM_FRAMES   90

// Celdas libres que como poco tiene que tener la carrera de un carril: un
// enemigo de carril metido en un hueco de una celda no patrulla nada.
#define LANE_MIN_RUN 3

// DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT (player.h), en ese orden.
static const s8 dirDx[4] = {  0, -1,  0,  1 };
static const s8 dirDy[4] = { -1,  0,  1,  0 };

typedef struct { u8 move, stepEvery, stepPx; u16 danger, vuln; } Kind;

// Velocidad y reparto peligrosa/vulnerable de cada especie, en fotogramas.
// Son los mandos de dificultad: el reparto de la torreta es el que decide
// si una sala con una torreta en medio es un rodeo o una espera.
// ESP_DESLIZA no usa los dos ultimos: su estado lo manda el movimiento.
static const Kind kinds[ENEMY_SPECIES_COUNT] = {
    { EMOVE_LANE,  1, 1, 100, 100 },
    { EMOVE_STILL, 1, 0, 220,  70 },
    { EMOVE_RING,  1, 2, 160,  80 },
    { EMOVE_STALK, 1, 1, 190,  60 },
    { EMOVE_SLIDE, 1, 3,   1,   1 },
    { EMOVE_GHOST, 2, 1,  50, 150 },
};

static const char *const speciesTags[ENEMY_SPECIES_COUNT] = { "PAT", "TOR", "RON", "ACE", "DES", "ESP" };

// Las celdas por las que la nave entra y sale de esta sala, rearmadas en
// cada Enemy_spawnGroup. Hacen de muro para todos, no solo al aparecer.
static bool doorTrajectory[MAZE_MAX_H][MAZE_MAX_W];

static s16 toTile(s16 px)
{
    return px >> MAZE_TILE_SHIFT;
}

static bool wallAt(s16 px, s16 py)
{
    return Maze_isWall(toTile(px), toTile(py));
}

static bool trajectoryAt(s16 px, s16 py)
{
    const s16 tx = toTile(px), ty = toTile(py);

    if ((tx < 0) || (ty < 0) || (tx >= MAZE_W) || (ty >= MAZE_H))
        return FALSE; // fuera de la sala ya es asunto del muro del borde

    return doorTrajectory[ty][tx];
}

// Las cuatro esquinas de la caja. Equivale a mirar solo el borde de avance
// porque el enemigo va siempre alineado en el eje que no recorre y avanza
// de pixel en pixel: las esquinas de atras caen en la celda que ya ocupaba.
static bool wallBox(s16 x, s16 y)
{
    return wallAt(x, y) || wallAt(x + BOX, y) || wallAt(x, y + BOX) || wallAt(x + BOX, y + BOX);
}

static bool trajBox(s16 x, s16 y)
{
    return trajectoryAt(x, y) || trajectoryAt(x + TRAJ, y) ||
           trajectoryAt(x, y + TRAJ) || trajectoryAt(x + TRAJ, y + TRAJ);
}

// El espectro no mira los muros, pero sigue sin poder pisar un pasillo de
// puerta ni salirse del anillo interior de la sala.
static bool outsideRing(s16 x, s16 y)
{
    return (x < MAZE_TILE_PX) || (y < MAZE_TILE_PX) ||
           (x > (s16) ((MAZE_W - 2) * MAZE_TILE_PX)) ||
           (y > (s16) ((MAZE_H - 2) * MAZE_TILE_PX));
}

static bool blockedBox(const Enemy *e, s16 x, s16 y)
{
    if (kinds[e->species].move == EMOVE_GHOST)
        return outsideRing(x, y) || trajBox(x, y);

    return wallBox(x, y) || trajBox(x, y);
}

// Avanza hasta px pixeles en e->dir y devuelve cuantos ha dado.
static u8 advance(Enemy *e, u8 px)
{
    const s16 dx = dirDx[e->dir], dy = dirDy[e->dir];
    u8 n;

    for (n = 0; n < px; n++)
    {
        if (blockedBox(e, (s16) (e->x + dx), (s16) (e->y + dy)))
            break;
        e->x += dx;
        e->y += dy;
    }

    return n;
}

static bool onGrid(const Enemy *e)
{
    return ((e->x & (MAZE_TILE_PX - 1)) == 0) && ((e->y & (MAZE_TILE_PX - 1)) == 0);
}

// Hacia la nave por el eje en el que esta mas lejos.
static u8 aimAt(const Enemy *e, s16 px, s16 py)
{
    const s16 dx = (s16) (px - e->x), dy = (s16) (py - e->y);
    const s16 ax = (dx < 0) ? (s16) -dx : dx;
    const s16 ay = (dy < 0) ? (s16) -dy : dy;

    if (ax >= ay)
        return (dx < 0) ? DIR_LEFT : DIR_RIGHT;

    return (dy < 0) ? DIR_UP : DIR_DOWN;
}

static u16 nextRng(Enemy *e)
{
    e->rng ^= (u16) (e->rng << 7);
    e->rng ^= (u16) (e->rng >> 9);
    e->rng ^= (u16) (e->rng << 8);

    return e->rng;
}

// Nueva direccion para ESP_DESLIZA: la primera de cuatro al azar que no
// tenga un muro pegado, para que el deslizamiento sea un deslizamiento y no
// un topetazo contra la pared de al lado.
static void pickSlideDir(Enemy *e)
{
    u8 t;

    for (t = 0; t < 4; t++)
    {
        e->dir = (u8) (nextRng(e) & 3);
        if (!blockedBox(e, (s16) (e->x + dirDx[e->dir]), (s16) (e->y + dirDy[e->dir])))
            return;
    }
}

static u16 stateLimit(const Enemy *e)
{
    const Kind *k = &kinds[e->species];

    return (e->state == ENEMY_DANGEROUS) ? k->danger : k->vuln;
}

// --- aparicion --------------------------------------------------------

// Cada celda por la que la nave se desliza justo despues de cruzar una
// puerta -- y, como el deslizamiento es reversible, las mismas que tiene
// que recorrer para salir por ella. borderX/borderY es la fila o columna
// del borde, dx/dy el paso hacia dentro y offset el arranque del vano.
static void markDoorTrajectory(s16 borderX, s16 borderY, s16 dx, s16 dy, u8 offset)
{
    u8 lane;

    for (lane = 0; lane < 2; lane++)
    {
        s16 x = (dx == 0) ? (s16) (offset + lane) : borderX;
        s16 y = (dy == 0) ? (s16) (offset + lane) : borderY;

        x += dx;
        y += dy;
        while (!Maze_isWall(x, y))
        {
            doorTrajectory[y][x] = TRUE;
            x += dx;
            y += dy;
        }
    }
}

// TRUE si la carrera recta que pasa por esta celda lleva una planta en
// algun punto (peticion del usuario: un enemigo y una hilera de plantas no
// comparten arista). Solo la mira ESP_PATRULLA, que rebota entre los dos
// muros de su carril y tarde o temprano barre la carrera entera.
static bool laneRunHasPlant(s16 tx, s16 ty, bool horiz)
{
    const s16 dx = horiz ? 1 : 0;
    const s16 dy = horiz ? 0 : 1;
    s16 x, y;

    for (x = tx, y = ty; !Maze_isWall(x, y); x += dx, y += dy)
        if (Plants_cellOccupied(x, y))
            return TRUE;

    for (x = (s16) (tx - dx), y = (s16) (ty - dy); !Maze_isWall(x, y); x -= dx, y -= dy)
        if (Plants_cellOccupied(x, y))
            return TRUE;

    return FALSE;
}

static bool blockedCell(s16 tx, s16 ty)
{
    if (Maze_isWall(tx, ty))
        return TRUE;
    if ((tx < 0) || (ty < 0) || (tx >= MAZE_W) || (ty >= MAZE_H))
        return TRUE;

    return doorTrajectory[ty][tx];
}

static bool cellFree(s16 tx, s16 ty, const s16 *tkX, const s16 *tkY, u8 taken)
{
    u8 j, open = 0;

    if (Maze_isWall(tx, ty) || doorTrajectory[ty][tx] || Plants_cellOccupied(tx, ty))
        return FALSE;
    if ((tx == MAZE_DOOR_COL) && (ty == MAZE_DOOR_ROW))
        return FALSE; // el centro es donde va la letra: no se tapa

    // Un hueco cerrado por los cuatro lados deja al enemigo de adorno.
    for (j = 0; j < 4; j++)
        if (!blockedCell((s16) (tx + dirDx[j]), (s16) (ty + dirDy[j])))
            open++;
    if (open == 0)
        return FALSE;

    for (j = 0; j < taken; j++)
    {
        const s16 ax = (s16) (tx - tkX[j]), ay = (s16) (ty - tkY[j]);

        if ((ax > -2) && (ax < 2) && (ay > -2) && (ay < 2))
            return FALSE; // pegados no
    }

    return TRUE;
}

// Carril 0/1: la fila de dentro del muro de arriba/abajo. Carril 2/3: la
// columna de dentro del muro de la izquierda/derecha.
// Celdas libres de la carrera recta que pasa por esta celda, ella incluida.
static u8 laneRunCells(s16 tx, s16 ty, bool horiz)
{
    const s16 dx = horiz ? 1 : 0;
    const s16 dy = horiz ? 0 : 1;
    s16 x, y;
    u8 n = 0;

    for (x = tx, y = ty; !blockedCell(x, y) && (n < 255); x += dx, y += dy)
        n++;
    for (x = (s16) (tx - dx), y = (s16) (ty - dy); !blockedCell(x, y) && (n < 255); x -= dx, y -= dy)
        n++;

    return n;
}

static bool findInLane(u8 lane, u16 seed, bool needClearRun, u8 minRun,
                        const s16 *tkX, const s16 *tkY, u8 taken, s16 *outX, s16 *outY)
{
    const bool horiz = (lane < 2);
    const s16 fixed = (lane == 0) ? 1 : (lane == 1) ? (s16) (MAZE_H - 2) : (lane == 2) ? 1 : (s16) (MAZE_W - 2);
    const u16 span = (u16) (horiz ? (MAZE_W - 2) : (MAZE_H - 2));
    const u16 start = seed % span;
    u16 i;

    for (i = 0; i < span; i++)
    {
        const s16 v = (s16) (1 + ((start + i) % span));
        const s16 tx = horiz ? v : fixed;
        const s16 ty = horiz ? fixed : v;

        if (cellFree(tx, ty, tkX, tkY, taken) &&
            (laneRunCells(tx, ty, horiz) >= minRun) &&
            (!needClearRun || !laneRunHasPlant(tx, ty, horiz)))
        {
            *outX = tx;
            *outY = ty;
            return TRUE;
        }
    }

    return FALSE;
}

static bool findInside(u16 seed, const s16 *tkX, const s16 *tkY, u8 taken, s16 *outX, s16 *outY)
{
    const u16 w = (u16) (MAZE_W - 2), h = (u16) (MAZE_H - 2);
    const u16 cells = (u16) (w * h);
    const u16 start = seed % cells;
    u16 i;

    for (i = 0; i < cells; i++)
    {
        const u16 c = (u16) ((start + i) % cells);
        const s16 tx = (s16) (1 + (c % w));
        const s16 ty = (s16) (1 + (c / w));

        if (cellFree(tx, ty, tkX, tkY, taken))
        {
            *outX = tx;
            *outY = ty;
            return TRUE;
        }
    }

    return FALSE;
}

static bool findSpot(u8 species, u16 seed, const s16 *tkX, const s16 *tkY, u8 taken,
                      s16 *outX, s16 *outY)
{
    const u8 move = kinds[species].move;
    const bool lanes = (move == EMOVE_LANE) || (move == EMOVE_RING);
    u8 i;

    if (lanes)
        for (i = 0; i < 4; i++)
            if (findInLane((u8) ((seed + i) & 3), seed, move == EMOVE_LANE,
                            (move == EMOVE_LANE) ? LANE_MIN_RUN : 1, tkX, tkY, taken, outX, outY))
                return TRUE;

    // Lo que queda (y el recambio de un carril imposible): cualquier celda
    // de dentro que este libre y fuera de los pasillos de puerta.
    return findInside(seed, tkX, tkY, taken, outX, outY);
}

static u8 startDir(u8 species, s16 ty, u16 seed, u8 i)
{
    const u8 move = kinds[species].move;
    const u8 bit = (u8) ((seed >> (i & 7)) & 1);

    if ((move == EMOVE_LANE) || (move == EMOVE_RING))
    {
        if ((ty == 1) || (ty == (s16) (MAZE_H - 2)))
            return bit ? DIR_LEFT : DIR_RIGHT;

        return bit ? DIR_UP : DIR_DOWN;
    }

    return (u8) ((seed >> (i & 3)) & 3);
}

void Enemy_spawnGroup(Enemy *list, u8 count, u8 speciesCap, u8 killedMask, u16 roomSeed,
                       bool doorN, bool doorE, bool doorS, bool doorW,
                       const u8 doorOffsets[4])
{
    s16 takenX[ENEMIES_MAX], takenY[ENEMIES_MAX];
    const u8 base = (u8) (roomSeed % speciesCap);
    u8 taken = 0, i, x, y;

    for (y = 0; y < MAZE_H; y++)
        for (x = 0; x < MAZE_W; x++)
            doorTrajectory[y][x] = FALSE;

    if (doorN) markDoorTrajectory(0, 0, 0, 1, doorOffsets[0]);
    if (doorE) markDoorTrajectory((s16) (MAZE_W - 1), 0, -1, 0, doorOffsets[1]);
    if (doorS) markDoorTrajectory(0, (s16) (MAZE_H - 1), 0, -1, doorOffsets[2]);
    if (doorW) markDoorTrajectory(0, 0, 1, 0, doorOffsets[3]);

    for (i = 0; i < count; i++)
    {
        Enemy *e = &list[i];
        const u8 sp = (u8) ((base + i) % speciesCap);
        s16 tx, ty;

        e->species = sp;
        e->alive = FALSE;
        e->deathTimer = 0;

        if (killedMask & (1 << i))
            continue;
        if (!findSpot(sp, (u16) (roomSeed + (u16) (i * 2731)), takenX, takenY, taken, &tx, &ty))
            continue; // sin sitio legal: esta sala se queda sin este

        takenX[taken] = tx;
        takenY[taken] = ty;
        taken++;

        e->x = (s16) (tx * MAZE_TILE_PX);
        e->y = (s16) (ty * MAZE_TILE_PX);
        e->dir = startDir(sp, ty, roomSeed, i);
        e->tick = 0;
        e->glide = 0;
        e->pause = 0;
        e->sawRest = FALSE;
        e->rng = (u16) ((roomSeed ^ (u16) (0x9E37 * (i + 1))) | 1);
        e->aimTimer = GHOST_AIM_FRAMES;
        // La fase del parpadeo es lo unico que sale del random() compartido:
        // la sala no puede recibirte igual dos veces (peticion del usuario).
        e->state = (random() & 1) ? ENEMY_VULNERABLE : ENEMY_DANGEROUS;
        e->stateTimer = (u16) (random() % stateLimit(e));
        e->alive = TRUE;
    }
}

// --- fotograma --------------------------------------------------------

void Enemy_update(Enemy *e, s16 playerX, s16 playerY, bool playerResting)
{
    const Kind *k;
    u8 px;

    if (!e->alive)
    {
        if (e->deathTimer > 0)
            e->deathTimer--;
        return;
    }

    k = &kinds[e->species];

    if (++e->tick < k->stepEvery)
        px = 0;
    else
    {
        e->tick = 0;
        px = k->stepPx;
    }

    switch (k->move)
    {
        case EMOVE_LANE:
            if ((px > 0) && (advance(e, px) < px))
                e->dir = (u8) ((e->dir + 2) & 3);
            break;

        case EMOVE_RING:
            // Sigue el muro por su derecha: desde una celda entera prueba
            // primero a girar hacia el muro, luego recto, y si tampoco
            // puede va girando al otro lado. Eso es lo que le hace
            // recorrer el contorno de la sala -- y el de cada obstaculo --
            // en vez de ir y venir. El giro solo se mira estando sobre la
            // rejilla: a medio camino la caja pisa dos celdas y la sonda
            // del lado no quiere decir nada.
            if (px > 0)
            {
                const u8 right = (u8) ((e->dir + 3) & 3);
                u8 turns = 0;

                if (onGrid(e) && !blockedBox(e, (s16) (e->x + dirDx[right]), (s16) (e->y + dirDy[right])))
                    e->dir = right;

                while ((advance(e, px) == 0) && (turns < 4))
                {
                    e->dir = (u8) ((e->dir + 1) & 3);
                    turns++;
                }
            }
            break;

        case EMOVE_STALK:
            // Una celda por cada parada de la nave: el paso se decide en el
            // flanco, no mientras sigue parada.
            if ((e->glide == 0) && playerResting && !e->sawRest)
            {
                e->dir = aimAt(e, playerX, playerY);
                e->glide = MAZE_TILE_PX;
            }
            e->sawRest = playerResting;

            if ((e->glide > 0) && (px > 0))
            {
                if (advance(e, 1) == 0)
                    e->glide = 0; // se le cruzo un muro: paso perdido
                else
                    e->glide--;
            }
            break;

        case EMOVE_SLIDE:
            if (e->pause > 0)
            {
                e->pause--;
                if (e->pause == 0)
                    pickSlideDir(e);
            }
            else if ((px > 0) && (advance(e, px) < px))
                e->pause = SLIDE_PAUSE_FRAMES;
            break;

        case EMOVE_GHOST:
            if (e->aimTimer > 0)
                e->aimTimer--;
            else if (onGrid(e))
            {
                e->dir = aimAt(e, playerX, playerY);
                e->aimTimer = GHOST_AIM_FRAMES;
            }
            if ((px > 0) && (advance(e, px) < px))
                e->dir = (u8) ((e->dir + 2) & 3);
            break;

        default: // EMOVE_STILL
            break;
    }

    // ESP_DESLIZA no parpadea por tiempo: vulnerable justo cuando esta
    // parado, que es la senal mas legible que hay -- se mueve, mata.
    if (k->move == EMOVE_SLIDE)
    {
        e->state = (e->pause > 0) ? ENEMY_VULNERABLE : ENEMY_DANGEROUS;
        return;
    }

    e->stateTimer++;
    if (e->stateTimer >= stateLimit(e))
    {
        e->stateTimer = 0;
        e->state = (e->state == ENEMY_DANGEROUS) ? ENEMY_VULNERABLE : ENEMY_DANGEROUS;
    }
}

void Enemy_kill(Enemy *e)
{
    e->alive = FALSE;
    e->deathTimer = ENEMY_DEATH_BLINK_FRAMES;
}

bool Enemy_isGone(const Enemy *e)
{
    return !e->alive && (e->deathTimer == 0);
}

bool Enemy_blinkVisible(const Enemy *e)
{
    if (e->alive)
        return TRUE;
    if (e->deathTimer == 0)
        return FALSE;

    return ((e->deathTimer / ENEMY_DEATH_BLINK_PERIOD) & 1) != 0;
}

bool Enemy_overlapsBox(const Enemy *e, s16 boxX, s16 boxY)
{
    return (boxX < e->x + MAZE_TILE_PX) && (e->x < boxX + MAZE_TILE_PX) &&
           (boxY < e->y + MAZE_TILE_PX) && (e->y < boxY + MAZE_TILE_PX);
}

u8 Enemy_frame(const Enemy *e)
{
    return (u8) ((e->species * 2) + ((e->state == ENEMY_VULNERABLE) ? 1 : 0));
}

const char *Enemy_speciesTag(u8 species)
{
    return (species < ENEMY_SPECIES_COUNT) ? speciesTags[species] : "???";
}
