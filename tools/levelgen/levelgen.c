// EL GENERADOR DE NIVELES, en el host.
//
// Incluye los maze.c y guidemap.c DE VERDAD contra los stubs de al lado, asi
// que lo que talla y valida es el mismo codigo que corria en el cartucho, no
// una copia. Con el random() de SGDK replicado bit a bit, los niveles que
// saca son los mismos que el juego generaba al arrancar -- es lo que permite
// hornearlos sin que la partida cambie.
//
// Replica la pasada de cartografia de main.c (scanPlanetPlantTotal), que es
// el orden exacto de llamadas: tamano de sala -> forma del mapa ->
// GuideMap_generate -> una Maze_generateRoom por sala.
#include <genesis.h>
#include "maze.h"
#include "guidemap.h"
#include "items.h"

// --- lo que guidemap.c y maze.c esperan de fuera
const TileSet mazeTiles;
const TileSet mapTiles;

// Al hornear, todas las puertas van abiertas: el cierre por letras es
// progreso de partida y no cambia el tallado (maze.h lo documenta: "the
// interior carve is unaffected").
bool Items_isUnlocked(u8 n) { (void) n; return TRUE; }
bool Items_revealedOnMap(u8 col, u8 row, char *outLetter)
{ (void) col; (void) row; (void) outLetter; return FALSE; }
void Menu_setVisible(bool v) { (void) v; }

void Items_drawHud(void) { }
void Hud_drawText(const char *s, u16 x, u16 y) { (void) s; (void) x; (void) y; }

#include "../../src/maze.c"
#include "../../src/guidemap.c"
#include "../../src/plants.c"

// --- los planetas, copiados tal cual de main.c: las siluetas y sizePresets[]
static const char *const map0[] = { "#" };
static const char *const map1[] = { "####", ".##.", ".##." };
static const char *const map2[] = { "##....", "##....", "######", "######" };
static const char *const map3[] = { "######", "..##..", "..##..", "######" };
static const char *const map4[] = { "..###..", "..###..", "#######", "..###..", "..###.." };
static const char *const map5[] = { "#######.", "##...##.", "##...###",
                                    "#######.", "##....##", "##....##" };
static const char *const map6[] = { "#.####.#", "########", "##....##",
                                    "##....##", "########", "#.####.#" };
static const char *const map7[] = { "##########", "##########", "....##....",
                                    "....##....", "....##....", "..######..",
                                    "..######..", "..######.." };

typedef struct { const char *const *mapa; u8 cols, rows, letras, roomW, roomH, variedad; } Preset;

static const Preset PRESETS[] = {
    { map0,  1, 1, 1, 12, 7, 0 },
    { map1,  4, 3, 1, 13, 7, 1 },
    { map2,  6, 4, 2, 14, 7, 2 },
    { map3,  6, 4, 3, 15, 7, 2 },
    { map4,  7, 5, 4, 16, 7, 3 },
    { map5,  8, 6, 5, 17, 7, 3 },
    { map6,  8, 6, 5, 18, 7, 4 },
    { map7, 10, 8, 5, 20, 7, 4 },
};
#define PRESET_COUNT ((int) (sizeof PRESETS / sizeof PRESETS[0]))

// CUANTAS GALAXIAS LLEVA EL CARTUCHO. Una galaxia son los 8 planetas que ya
// ensena el menu, uno por tamano -- el .bin de una sola galaxia era justo lo
// que habia hasta ahora. Los planetas van en orden de galaxia: el indice de
// (galaxia, preset) es galaxia * PRESET_COUNT + preset.
#define GALAXIAS 8
#define PLANETAS (GALAXIAS * PRESET_COUNT)

static u16 mapSeed;

// Igual que main.c: misma (col,row) bajo el mismo mapSeed, misma sala.
static u16 roomSeedFor(u8 col, u8 row)
{
    u32 h = mapSeed;

    h = h * 374761393u + col;
    h = h * 668265263u + row;
    h ^= h >> 15;
    return (u16) h;
}

// Igual que el de main.c: la puerta del enlace con la sala de extraccion no
// es una puerta del arbol, asi que GuideMap_doorOffset no tiene un valor con
// sentido para ella y manda insertLinkOffset. Es UNA sala por planeta, y
// saltarse esto fue justo lo que hizo que 8 de las 118 salieran distintas.
static u8 doorOffsetFor(u8 col, u8 row, u8 dir)
{
    if ((col == insertLinkCol) && (row == insertLinkRow) && (dir == insertLinkDir))
        return insertLinkOffset;

    return GuideMap_doorOffset(col, row, dir);
}

// ===================== EL FORMATO HORNEADO =====================
//
// Por sala, lo que la consola necesita para dibujarla y colisionar sin
// generarla. Dos bits por celda bastan porque Maze_draw solo pregunta "se
// dibuja esto?" -- shapeEdge sale de outsideShape y ninguna de las dos hace
// falta guardarla:
//
//   0  no se dibuja (recorte de la silueta; para Maze_isWall sigue siendo muro)
//   1  suelo
//   2  muro
//   3  libre
//
// Las tramas de muro tampoco se guardan: salen de (x, y, tileSeed) con
// cellVariant(), que es lo que hace que una sala quepa en tan poco.
//
// La rejilla se hornea con TODAS las puertas cerradas, igual que hace la
// pasada de cartografia; al cargar, el que abre los vanos de las puertas que
// toquen es punchBorderDoor() con doorMask y los offsets, que tambien van aqui.
#define CELDA_NADA  0
#define CELDA_SUELO 1
#define CELDA_MURO  2

// Cabecera de sala: 11 B de metadatos, luego las plantas y luego las celdas.
typedef struct
{
    u16 tileSeed;
    u8 w, h;
    u8 hubX, hubY;
    u8 doorMask;            // bit por DOOR_N/E/S/W
    u8 doorOff[4];
    u8 plantCount[PLANTS_LINES_PER_ROOM];
} CabeceraSala;

static u8 horno[4u << 20];
static unsigned hornoLen;

static void escribeByte(u8 b)
{
    if (hornoLen < sizeof horno)
        horno[hornoLen++] = b;
}

static u8 codigoCelda(s16 x, s16 y)
{
    if (outsideShape[y][x] && !shapeEdge[y][x])
        return CELDA_NADA;
    return (grid[y][x] == PATH) ? CELDA_SUELO : CELDA_MURO;
}

// Hornea la sala que el generador acaba de dejar en sus arrays. Devuelve
// cuantos bytes ha ocupado.
static unsigned horneaSala(u16 semilla, u8 doorMask, const u8 doorOff[4])
{
    const unsigned ini = hornoLen;
    const u8 w = Maze_roomW(), h = Maze_roomH();
    s16 x, y;
    int l, i;
    u8 acc = 0;
    int bits = 0;

    escribeByte((u8) (semilla & 0xFF));
    escribeByte((u8) (semilla >> 8));
    escribeByte(w);
    escribeByte(h);
    escribeByte((u8) Maze_hubX());
    escribeByte((u8) Maze_hubY());
    escribeByte(doorMask);
    for (i = 0; i < 4; i++)
        escribeByte(doorOff[i]);
    for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
        escribeByte(curCount[l]);
    for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
        for (i = 0; i < curCount[l]; i++)
        {
            escribeByte(curCol[l][i]);
            escribeByte(curRow[l][i]);
        }

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            acc = (u8) (acc | (u8) (codigoCelda(x, y) << bits));
            bits += 2;
            if (bits == 8)
            {
                escribeByte(acc);
                acc = 0;
                bits = 0;
            }
        }
    if (bits)
        escribeByte(acc);

    return hornoLen - ini;
}

// --- la vuelta: reconstruye la sala desde el horno y la compara con la que
// --- el generador tiene viva ahora mismo. Es la prueba de que el formato
// --- esta completo: si falta un bit, aqui salta.
static int compruebaSala(unsigned off, int esExtraccion, int *celdasMal, int *plantasMal)
{
    const u8 *p = &horno[off];
    const u16 semilla = (u16) (p[0] | (p[1] << 8));
    const u8 w = p[2], h = p[3], hubX = p[4], hubY = p[5];
    const u8 *cuentas = &p[11];
    const u8 *plantas = &p[11 + PLANTS_LINES_PER_ROOM];
    unsigned nplantas = 0;
    const u8 *celdas;
    s16 x, y;
    int l, i, fallos = 0;

    for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
        nplantas += cuentas[l];
    celdas = plantas + (nplantas * 2);

    if ((w != Maze_roomW()) || (h != Maze_roomH()) ||
        (hubX != Maze_hubX()) || (hubY != Maze_hubY()))
        fallos++;

    // Las plantas, en el mismo orden
    {
        const u8 *q = plantas;

        for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
        {
            if (cuentas[l] != curCount[l])
                (*plantasMal)++;
            for (i = 0; i < cuentas[l]; i++, q += 2)
                if ((q[0] != curCol[l][i]) || (q[1] != curRow[l][i]))
                    (*plantasMal)++;
        }
    }

    // Las celdas: reconstruir el valor de grid y el "se dibuja"
    tileSeed = semilla;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            const unsigned n = (unsigned) ((y * w) + x);
            const u8 cod = (u8) ((celdas[n / 4] >> ((n % 4) * 2)) & 3);
            const int noDibuja = (cod == CELDA_NADA);
            const int noDibujaVivo = (outsideShape[y][x] && !shapeEdge[y][x]) ? 1 : 0;
            // La sala de extraccion lleva trama fija y uniforme (spec 27), no
            // la sorteada por celda. No hace falta un bit en el formato: va
            // en su propia ranura del planeta, asi que el cargador ya sabe
            // cual es.
            const u8 valor = (cod == CELDA_SUELO)
                             ? PATH
                             : esExtraccion
                               ? (u8) INSERT_WALL_VARIANT
                               : (u8) (CELL_MURO_BASE + cellVariant(x, y, CELL_MURO_COUNT));

            if (noDibuja != noDibujaVivo)
                (*celdasMal)++;
            else if (valor != grid[y][x])
                (*celdasMal)++;
        }

    return fallos;
}

// --- EL REGISTRO DE PLANETA ---
//
// No repite las puertas ni sus offsets: eso ya va en la cabecera de cada
// sala, asi que guideMap se reconstruye leyendolas. La unica resta es la
// puerta del enlace con la sala de extraccion, que main.c anade a la mascara
// de la sala pero que NO es una puerta del arbol.
// 24 B de cabecera: preset, cols, rows, letras, inicio, meta, el enlace de
// extraccion (col,row,dir,offset), las 5+5 celdas de las letras y el offset de
// la sala de extraccion. Detras van el mapa de bits de que celdas son sala y
// el offset de cada una.
// 26 B de cabecera: preset, cols, rows, letras, inicio, meta, el enlace de
// extraccion (col,row,dir,offset), las 5+5 celdas de las letras y el offset de
// la sala de extraccion. Detras van el mapa de bits de que celdas son sala y
// el offset de cada una.
#define PLANETA_META 26

static void escribeU16(u16 v) { escribeByte((u8) (v & 0xFF)); escribeByte((u8) (v >> 8)); }

// LOS OFFSETS DE SALA VAN EN u32 Y ABSOLUTOS (del arranque del fichero, no del
// payload). En u16 relativos no cabian: el techo eran 64 KB de payload, o sea
// unas 950 salas, y la peticion era 1000. Cuestan 2 B por sala -- 2 KB en el
// cartucho entero -- y a cambio el formato deja de tener techo y el cargador
// deja de tener que sumar la cabecera.
#define CABECERA (8 + (4 * PLANETAS))

static void escribeU32abs(unsigned off)
{
    const unsigned v = off + CABECERA;

    escribeByte((u8) (v >> 24));
    escribeByte((u8) (v >> 16));
    escribeByte((u8) (v >> 8));
    escribeByte((u8) v);
}

static unsigned horneaPlaneta(int preset, const unsigned offSala[MAX_MAP_ROWS][MAX_MAP_COLS],
                              unsigned offExtraccion)
{
    const unsigned ini = hornoLen;
    u8 col, row;
    int i;
    u8 acc = 0;
    int bits = 0;

    escribeByte((u8) preset);
    escribeByte(mapCols);
    escribeByte(mapRows);
    escribeByte(itemCount);
    escribeByte(startCol);  escribeByte(startRow);
    escribeByte(goalCol);   escribeByte(goalRow);
    escribeByte(insertLinkCol); escribeByte(insertLinkRow);
    escribeByte(insertLinkDir); escribeByte(insertLinkOffset);
    for (i = 0; i < ITEM_COUNT; i++) escribeByte(itemCol[i]);
    for (i = 0; i < ITEM_COUNT; i++) escribeByte(itemRow[i]);
    escribeU32abs(offExtraccion);

    // El mapa de bits de que celdas son sala, en el orden row*cols+col
    for (row = 0; row < mapRows; row++)
        for (col = 0; col < mapCols; col++)
        {
            acc = (u8) (acc | (u8) ((guideMap[row][col].type == CELL_ROOM ? 1 : 0) << bits));
            if (++bits == 8) { escribeByte(acc); acc = 0; bits = 0; }
        }
    if (bits) escribeByte(acc);

    // Y el offset de cada sala, en el mismo orden que el mapa de bits
    for (row = 0; row < mapRows; row++)
        for (col = 0; col < mapCols; col++)
            if (guideMap[row][col].type == CELL_ROOM)
                escribeU32abs(offSala[row][col]);

    return hornoLen - ini;
}

// La vuelta del planeta: reconstruye guideMap desde el horno y lo compara con
// el vivo, puertas y offsets incluidos.
static void compruebaPlaneta(unsigned off, int *mal)
{
    const u8 *p = &horno[off];
    const u8 cols = p[1], rows = p[2];
    const u8 iCol = p[8], iRow = p[9], iDir = p[10];
    const u8 *bitmap = &p[PLANETA_META];
    const unsigned nbits = (unsigned) (cols * rows);
    const u8 *offs = bitmap + ((nbits + 7) / 8);
    // Los offsets de sala son absolutos del fichero; dentro de `horno` hay que
    // restarles la cabecera, que aqui todavia no esta delante.
    unsigned n = 0;
    u8 col, row;
    int i;

    if ((p[0] < 0) || (cols != mapCols) || (rows != mapRows) || (p[3] != itemCount) ||
        (p[4] != startCol) || (p[5] != startRow) || (p[6] != goalCol) || (p[7] != goalRow) ||
        (iCol != insertLinkCol) || (iRow != insertLinkRow) || (iDir != insertLinkDir) ||
        (p[11] != insertLinkOffset))
        (*mal)++;
    for (i = 0; i < ITEM_COUNT; i++)
        if ((p[12 + i] != itemCol[i]) || (p[12 + ITEM_COUNT + i] != itemRow[i]))
            (*mal)++;

    for (row = 0; row < rows; row++)
        for (col = 0; col < cols; col++)
        {
            const unsigned b = (unsigned) ((row * cols) + col);
            const int esSala = (bitmap[b / 8] >> (b % 8)) & 1;

            if (esSala != (guideMap[row][col].type == CELL_ROOM ? 1 : 0))
            {
                (*mal)++;
                continue;
            }
            if (!esSala)
                continue;

            // La cabecera de la sala trae la mascara de puertas; quitarle la
            // del enlace devuelve exactamente las del arbol.
            {
                const unsigned os = (unsigned) (((unsigned) offs[n * 4] << 24) |
                                                 ((unsigned) offs[(n * 4) + 1] << 16) |
                                                 ((unsigned) offs[(n * 4) + 2] << 8) |
                                                 offs[(n * 4) + 3]) - CABECERA;
                const u8 *h = &horno[os];
                u8 mascara = h[6];
                const u8 *doff = &h[7];
                int d;

                if ((col == iCol) && (row == iRow))
                    mascara = (u8) (mascara & ~(1u << iDir));

                for (d = 0; d < 4; d++)
                {
                    const int hay = (mascara >> d) & 1;
                    const int vivo = (d == DOOR_N) ? guideMap[row][col].doorN
                                   : (d == DOOR_E) ? guideMap[row][col].doorE
                                   : (d == DOOR_S) ? guideMap[row][col].doorS
                                                   : guideMap[row][col].doorW;
                    const u8 ovivo = (d == DOOR_N) ? guideMap[row][col].doorOffsetN
                                    : (d == DOOR_E) ? guideMap[row][col].doorOffsetE
                                    : (d == DOOR_S) ? guideMap[row][col].doorOffsetS
                                                    : guideMap[row][col].doorOffsetW;

                    if (hay != (vivo ? 1 : 0))
                        (*mal)++;
                    else if (hay && (doff[d] != ovivo))
                        (*mal)++;
                }
                n++;
            }
        }
}

// --- LA PRUEBA DE FUEGO: el cargador DE VERDAD ---
//
// Guarda lo que el generador acaba de dejar, llama a Maze_loadRoom y
// Plants_loadForRoom (los de maze.c y plants.c, los que van en el cartucho) y
// compara celda a celda. Si el cargador y el generador no dejan el mismo
// estado, salta aqui y no en el emulador.
static int compruebaCargador(unsigned off, int esExtraccion)
{
    static u8 gridV[MAZE_MAX_H][MAZE_MAX_W];
    static bool nodibujaV[MAZE_MAX_H][MAZE_MAX_W];
    static bool cerradaV[MAZE_MAX_H][MAZE_MAX_W];
    static u8 colV[PLANTS_LINES_PER_ROOM][PLANTS_MAX_PER_LINE];
    static u8 rowV[PLANTS_LINES_PER_ROOM][PLANTS_MAX_PER_LINE];
    static u8 cuentaV[PLANTS_LINES_PER_ROOM];
    const u8 wV = Maze_roomW(), hV = Maze_roomH();
    bool abierta[4] = { FALSE, FALSE, FALSE, FALSE };
    int fallos = 0, l, i;
    s16 x, y;

    for (y = 0; y < hV; y++)
        for (x = 0; x < wV; x++)
        {
            gridV[y][x] = grid[y][x];
            nodibujaV[y][x] = (outsideShape[y][x] && !shapeEdge[y][x]) ? TRUE : FALSE;
            cerradaV[y][x] = lockedDoorCell[y][x];
        }
    for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
    {
        cuentaV[l] = curCount[l];
        for (i = 0; i < PLANTS_MAX_PER_LINE; i++)
        {
            colV[l][i] = curCol[l][i];
            rowV[l][i] = curRow[l][i];
        }
    }

    // La horneada de las salas de rejilla trae las puertas cerradas, que es
    // como las dejo la pasada de cartografia, asi que se cargan con abierta[]
    // todo FALSE. La de extraccion es al contrario: es un pasillo entre sus
    // dos puertas y esas NO se cierran nunca, asi que va horneada con ellas
    // abiertas y hay que cargarla diciendoselo.
    if (esExtraccion)
    {
        const u8 mascara = horno[off + 6];

        for (i = 0; i < 4; i++)
            abierta[i] = ((mascara >> i) & 1) ? TRUE : FALSE;
    }
    memset(grid, 0xAA, sizeof grid);
    memset(outsideShape, 0xAA, sizeof outsideShape);
    for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
        curCount[l] = 99;

    Maze_loadRoom(&horno[off], esExtraccion, abierta);
    Plants_loadForRoom(&horno[off], 0);

    if ((Maze_roomW() != wV) || (Maze_roomH() != hV))
        fallos++;
    for (y = 0; y < hV; y++)
        for (x = 0; x < wV; x++)
        {
            const bool nd = (outsideShape[y][x] && !shapeEdge[y][x]) ? TRUE : FALSE;

            if ((grid[y][x] != gridV[y][x]) || (nd != nodibujaV[y][x]) ||
                (lockedDoorCell[y][x] != cerradaV[y][x]))
                fallos++;
        }
    for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
    {
        if (curCount[l] != cuentaV[l])
            fallos++;
        for (i = 0; i < curCount[l]; i++)
            if ((curCol[l][i] != colV[l][i]) || (curRow[l][i] != rowV[l][i]))
                fallos++;
    }

    return fallos;
}

// El hash de la sala recien tallada. La misma aritmetica que la del parche de
// comparacion en main.c: compararlos es lo que prueba que el host saca los
// mismos niveles que el cartucho.
static u16 hashSala(int plantas)
{
    u16 h = 0xACE1;
    s16 x, y;

    for (y = 0; y < (s16) Maze_roomH(); y++)
        for (x = 0; x < (s16) Maze_roomW(); x++)
        {
            const u16 b = (u16) ((plantas ? Maze_isPlantSafe(x, y)
                                          : Maze_isWall(x, y)) ? 1 : 0);

            h = (u16) ((h * 31u) + b);
        }

    return h;
}

// El hash de las plantas de la sala: cuantas hay en cada linea y donde.
static u16 hashPlantas(void)
{
    u16 h = 0xACE1;
    int l, i;

    for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
    {
        h = (u16) ((h * 31u) + curCount[l]);
        for (i = 0; i < PLANTS_MAX_PER_LINE; i++)
            h = (u16) ((h * 31u) + (u16) ((curCol[l][i] * 31u) + curRow[l][i]));
    }

    return h;
}

// Cartografia un planeta entero. Devuelve cuantas salas ha tallado.
static u16 totalM, totalV, totalP;
static int hashes;
static int plantasTotal, plantasMax;
static int celdasMal, plantasMalas, metaMal, salasHorneadas, planetasMal, cargadorMal;
static unsigned bytesSalas, bytesPlanetas;
static unsigned offPlaneta[PLANETAS];

// ===================== EL FILTRO =====================
//
// No toda semilla da un planeta que merezca ir en el cartucho. Esto genera uno
// EN VIVO (sin hornear) y lo mide, para poder descartarlo antes de gastar
// bytes: cuantas salas le ha salido el tunel, si alguna cayo en el apano de
// tallado (grafo de deslizamiento vacio: sala sin validar y sin plantas) y si
// el generador tuvo que bajarle las letras.
static void generaVivo(int preset, u16 semilla)
{
    const Preset *p = &PRESETS[preset];

    mapCols = p->cols;
    mapRows = p->rows;
    itemCount = p->letras;
    mapSeed = semilla;
    setRandomSeed(mapSeed);
    Maze_setRoomSize(p->roomW, p->roomH, p->variedad);
    GuideMap_setShape(p->mapa, p->cols, p->rows);
    GuideMap_generate();
    Maze_setActiveMapSlot(0);
    Maze_clearRoomCache();
}

// Cuantas celdas de la silueta podrian ser sala: el techo contra el que se
// mide lo que ha llenado el tunel.
static int huecosDeLaSilueta(int preset)
{
    const Preset *p = &PRESETS[preset];
    int n = 0, col, row;

    for (row = 0; row < p->rows; row++)
        for (col = 0; col < p->cols; col++)
            if (p->mapa[row][col] == '#')
                n++;

    return n;
}

typedef struct { int salas, apanos, letras, plantas, plantasMin; } Calidad;

static void mide(int preset, u16 semilla, Calidad *out)
{
    u8 col, row;

    generaVivo(preset, semilla);
    out->salas = 0;
    out->apanos = 0;
    out->plantas = 0;
    out->plantasMin = 999;

    for (row = 0; row < mapRows; row++)
        for (col = 0; col < mapCols; col++)
        {
            const MapCell celda = guideMap[row][col];
            const int esEnlace = (col == insertLinkCol) && (row == insertLinkRow);
            bool dN, dE, dS, dW;
            u8 offs[4];

            if (celda.type != CELL_ROOM)
                continue;

            dN = celda.doorN || (esEnlace && (insertLinkDir == DOOR_N));
            dE = celda.doorE || (esEnlace && (insertLinkDir == DOOR_E));
            dS = celda.doorS || (esEnlace && (insertLinkDir == DOOR_S));
            dW = celda.doorW || (esEnlace && (insertLinkDir == DOOR_W));
            offs[DOOR_N] = doorOffsetFor(col, row, DOOR_N);
            offs[DOOR_E] = doorOffsetFor(col, row, DOOR_E);
            offs[DOOR_S] = doorOffsetFor(col, row, DOOR_S);
            offs[DOOR_W] = doorOffsetFor(col, row, DOOR_W);

            Maze_generateRoom(dN, dE, dS, dW, dN, dE, dS, dW, offs,
                              roomSeedFor(col, row), (u8) ((row * MAX_MAP_COLS) + col));
            if (Maze_slideNodeCount() == 0)
                out->apanos++;
            Plants_spawnForRoom(roomSeedFor(col, row), (u16) ((row * MAX_MAP_COLS) + col));
            out->plantas += Plants_lastRoomCount();
            if ((int) Plants_lastRoomCount() < out->plantasMin)
                out->plantasMin = Plants_lastRoomCount();
            out->salas++;
        }

    out->letras = itemCount;
}

// El reparto de calidades sobre muchas semillas, para elegir los umbrales con
// datos y no a ojo. Modo -f.
static void reparto(int cuantas)
{
    int preset;

    printf("preset huecos   salas(min/med/max)  apanos  letras<pedidas  plantas min=0\n");
    for (preset = 0; preset < PRESET_COUNT; preset++)
    {
        const int huecos = huecosDeLaSilueta(preset);
        int minS = 999, maxS = 0, sumS = 0, conApano = 0, pocasLetras = 0, sinPlantas = 0;
        int i;

        for (i = 0; i < cuantas; i++)
        {
            Calidad c;

            mide(preset, (u16) (0x1000 + (i * 7919)), &c);
            if (c.salas < minS) minS = c.salas;
            if (c.salas > maxS) maxS = c.salas;
            sumS += c.salas;
            if (c.apanos) conApano++;
            if (c.letras < PRESETS[preset].letras) pocasLetras++;
            if (c.plantasMin == 0) sinPlantas++;
        }
        printf("  %d     %3d      %3d/%5.1f/%3d      %4d      %4d            %4d\n",
               preset, huecos, minS, (double) sumS / cuantas, maxS,
               conApano, pocasLetras, sinPlantas);
    }
}

// EL MAPA DE UN PLANETA, en un hash: que celdas son sala y por donde tienen
// puerta. SIN los offsets de puerta a proposito -- lo que el jugador reconoce
// de un planeta es su forma, no por que punto del borde cruza cada vano, asi
// que dos tuneles iguales con offsets distintos cuentan como repetidos.
static u32 hashMapa(void)
{
    u32 h = 2166136261u;
    u8 col, row;

    for (row = 0; row < mapRows; row++)
        for (col = 0; col < mapCols; col++)
        {
            const MapCell *c = &guideMap[row][col];
            const u32 v = (u32) ((c->type == CELL_ROOM ? 1 : 0) |
                                 (c->doorN << 1) | (c->doorE << 2) |
                                 (c->doorS << 3) | (c->doorW << 4));

            h = (h ^ v) * 16777619u;
        }

    return h;
}

static u32 mapasVistos[PLANETAS];
static int mapasN;
static u16 semillaDe[PLANETAS];

// Una silueta de una o dos celdas ofrece UN mapa y nada mas, asi que ahi
// "repetido" no quiere decir nada: lo que cambia entre galaxias es el interior
// de la sala, que es todo lo que hay que reconocer. Sin esta puerta, el
// planeta de 1x1 se come las 64 candidatas en cada galaxia para nada.
static int mapaPuedeVariar(int preset) { return huecosDeLaSilueta(preset) >= 4; }

static int mapaRepetido(void)
{
    const u32 h = hashMapa();
    int i;

    for (i = 0; i < mapasN; i++)
        if (mapasVistos[i] == h)
            return 1;

    return 0;
}

// Cuantas salas, como poco, para que el planeta merezca el sitio: un tunel
// tacano deja la silueta medio vacia y se juega en un rato. En porcentaje de
// los huecos que la silueta ofrece, que es lo que hace que valga igual para
// el planeta de 1 sala y para el de 44.
#define SALAS_MIN_PCT 60

// Cuantas semillas se prueban por planeta antes de quedarse con la mejor que
// haya salido. Acotado a proposito y no "hasta que una pase": el planeta de
// 1x1 tiene UN mapa posible, asi que el rechazo de repetidos lo deja sin
// candidatos en la segunda galaxia y un bucle abierto no terminaria nunca.
#define CANDIDATAS 64

// Busca, a partir de `desde`, un planeta que pase el filtro y cuyo mapa no se
// haya visto. Para en la primera que vale; si ninguna de las CANDIDATAS vale,
// se queda con la mejor que haya salido (mas salas, y sin repetir si se puede)
// -- mas vale un planeta algo flojo que un hueco en el cartucho.
static u16 eligeSemilla(int preset, u16 desde, Calidad *out, int *piso_out, int *probadas)
{
    const int piso = ((huecosDeLaSilueta(preset) * SALAS_MIN_PCT) + 99) / 100;
    Calidad mejor;
    u16 mejorSem = desde;
    int mejorPuntos = -1;
    int i;

    *piso_out = piso;
    mejor.salas = 0;

    for (i = 0; i < CANDIDATAS; i++)
    {
        const u16 sem = (u16) (desde + (i * 7919));
        Calidad c;
        int repetido, puntos;

        mide(preset, sem, &c);

        // Esos dos no entran nunca: una sala sin grafo de deslizamiento es una
        // sala sin validar y sin plantas, y un planeta con menos letras de las
        // que pide su tamano no es ese planeta.
        if (c.apanos || (c.letras != PRESETS[preset].letras))
            continue;

        repetido = mapaPuedeVariar(preset) ? mapaRepetido() : 0;
        puntos = (c.salas * 2) + (repetido ? 0 : 1);
        if (puntos > mejorPuntos)
        {
            mejorPuntos = puntos;
            mejorSem = sem;
            mejor = c;
        }
        if ((c.salas >= piso) && !repetido)
            break;
    }

    *probadas = (i < CANDIDATAS) ? (i + 1) : CANDIDATAS;

    // Deja el elegido puesto otra vez: el hash que se apunta tiene que ser el
    // suyo y no el del ultimo que se probo.
    mide(preset, mejorSem, &mejor);
    mapasVistos[mapasN++] = hashMapa();
    *out = mejor;

    return mejorSem;
}

static int planeta(int indice, int preset, u16 semilla, int verboso)
{
    const Preset *p = &PRESETS[preset];
    static unsigned offSala[MAX_MAP_ROWS][MAX_MAP_COLS];
    unsigned offExtraccion = 0;
    u8 col, row;
    int salas = 0;

    memset(offSala, 0, sizeof offSala);

    totalM = 0xACE1;
    totalV = 0xACE1;
    totalP = 0xACE1;

    mapCols = p->cols;
    mapRows = p->rows;
    itemCount = p->letras;
    mapSeed = semilla;
    setRandomSeed(mapSeed);
    Maze_setRoomSize(p->roomW, p->roomH, p->variedad);
    GuideMap_setShape(p->mapa, p->cols, p->rows);
    GuideMap_generate();
    Maze_setActiveMapSlot(0);
    Maze_clearRoomCache();

    for (row = 0; row < mapRows; row++)
        for (col = 0; col < mapCols; col++)
        {
            const MapCell celda = guideMap[row][col];
            bool dN, dE, dS, dW;
            u8 offs[4];
            u16 semillaSala;
            int esEnlace;

            if (celda.type != CELL_ROOM)
                continue;

            esEnlace = (col == insertLinkCol) && (row == insertLinkRow);
            dN = celda.doorN || (esEnlace && (insertLinkDir == DOOR_N));
            dE = celda.doorE || (esEnlace && (insertLinkDir == DOOR_E));
            dS = celda.doorS || (esEnlace && (insertLinkDir == DOOR_S));
            dW = celda.doorW || (esEnlace && (insertLinkDir == DOOR_W));
            offs[DOOR_N] = doorOffsetFor(col, row, DOOR_N);
            offs[DOOR_E] = doorOffsetFor(col, row, DOOR_E);
            offs[DOOR_S] = doorOffsetFor(col, row, DOOR_S);
            offs[DOOR_W] = doorOffsetFor(col, row, DOOR_W);
            semillaSala = roomSeedFor(col, row);

            Maze_generateRoom(dN, dE, dS, dW, dN, dE, dS, dW, offs, semillaSala,
                              (u8) ((row * MAX_MAP_COLS) + col));
            // Las plantas, igual que main.c: justo detras del tallado y con la
            // misma semilla de sala. Dependen del grafo de deslizamiento, que
            // acaba de construir el generador.
            Plants_spawnForRoom(semillaSala, (u16) ((row * MAX_MAP_COLS) + col));
            salas++;
            {
                const u8 mascara = (u8) ((dN ? 1 : 0) | (dE ? 2 : 0) |
                                         (dS ? 4 : 0) | (dW ? 8 : 0));
                const unsigned off = hornoLen;

                bytesSalas += horneaSala(semillaSala, mascara, offs);
                metaMal += compruebaSala(off, 0, &celdasMal, &plantasMalas);
                offSala[row][col] = off;
                cargadorMal += compruebaCargador(off, 0);
                salasHorneadas++;
            }
            {
                const u16 hm = hashSala(0), hp = hashSala(1);
                const u16 hpl = hashPlantas();
                int l, k;

                plantasTotal += Plants_lastRoomCount();
                if (Plants_lastRoomCount() > plantasMax)
                    plantasMax = Plants_lastRoomCount();
                for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
                    for (k = 0; k < curCount[l]; k++)
                        (void) k;
                if (hashes)
                    printf("PLT p%d %u,%u n%u h%04X\n", indice, col, row,
                           Plants_lastRoomCount(), hpl);
                totalP = (u16) ((totalP * 31u) + hpl);

                if (hashes)
                    printf("CMP p%d %u,%u s%04X m%04X v%04X\n", indice, col, row,
                           semillaSala, hm, hp);
                totalM = (u16) ((totalM * 31u) + hm);
                totalV = (u16) ((totalV * 31u) + hp);
            }

            if (verboso)
            {
                s16 x, y;

                printf("\n--- sala %u,%u  semilla %04X  puertas %c%c%c%c  %dx%d  hub %u,%u\n",
                       col, row, semillaSala, dN ? 'N' : '.', dE ? 'E' : '.',
                       dS ? 'S' : '.', dW ? 'W' : '.', Maze_roomW(), Maze_roomH(),
                       Maze_hubX(), Maze_hubY());
                for (y = 0; y < Maze_roomH(); y++)
                {
                    printf("    ");
                    for (x = 0; x < Maze_roomW(); x++)
                        printf("%c", Maze_isWall(x, y) ? '#' :
                                     Maze_isPlantSafe(x, y) ? ',' : '.');
                    printf("\n");
                }
            }
        }

    // La sala de extraccion, la primera que ve el jugador: fuera de la rejilla
    // y con su propio generador. Los parametros, calculados como en main.c.
    {
        const u8 dirSala = (u8) ((insertLinkDir + 2) & 3);
        const u8 dirMenu = (u8) ((dirSala + 1) & 3);
        const u8 offMenu = ((dirMenu == DOOR_N) || (dirMenu == DOOR_S))
                           ? (u8) (Maze_roomW() / 2) : (u8) (Maze_roomH() / 2);

        Maze_generateInsertionRoom(dirSala, insertLinkOffset, dirMenu, offMenu, semilla);
        {
            u8 offs[4] = { 0, 0, 0, 0 };
            const u8 mascara = (u8) ((1u << dirSala) | (1u << dirMenu));
            unsigned off;

            offs[dirSala] = insertLinkOffset;
            offs[dirMenu] = offMenu;
            // La de extraccion no lleva plantas (su grafo de deslizamiento no
            // existe, asi que Plants_spawnForRoom se planta en 0), pero el
            // formato es el mismo: hay que vaciar los recuentos a mano.
            {
                int l;

                for (l = 0; l < PLANTS_LINES_PER_ROOM; l++)
                    curCount[l] = 0;
            }
            off = hornoLen;
            bytesSalas += horneaSala(semilla, mascara, offs);
            metaMal += compruebaSala(off, 1, &celdasMal, &plantasMalas);
            offExtraccion = off;
            cargadorMal += compruebaCargador(off, 1);
            salasHorneadas++;
        }
        if (hashes)
            printf("EXT p%d dir%u off%u menu%u/%u m%04X v%04X\n", indice, dirSala,
                   insertLinkOffset, dirMenu, offMenu, hashSala(0), hashSala(1));
    }

    // El registro del planeta va detras de sus salas, que es lo que le permite
    // apuntarlas por offset absoluto.
    offPlaneta[indice] = hornoLen;
    bytesPlanetas += horneaPlaneta(preset, offSala, offExtraccion);
    compruebaPlaneta(offPlaneta[indice], &planetasMal);

    return salas;
}

static int plantasMal_total(void) { return plantasMalas; }

// --- EL FICHERO ---
//
// Cabecera: 'OVNI', version, cuantos planetas, cuantas galaxias, cuantos
// planetas lleva cada galaxia, y el offset absoluto de cada registro de
// planeta. Los dos bytes de galaxia iban sin usar hasta ahora, asi que las
// galaxias no han costado un byte de formato.
//
// El fichero tal cual, en memoria: la cabecera delante del horno. Es lo que
// comen los cargadores de la consola, asi que es lo que hay que darles para
// probarlos aqui -- y lo que se escribe a disco, el mismo buffer.
static u8 fichero[CABECERA + sizeof horno];

static unsigned montaFichero(void)
{
    unsigned i;

    memset(fichero, 0, CABECERA);
    fichero[0] = 'O'; fichero[1] = 'V'; fichero[2] = 'N'; fichero[3] = 'I';
    fichero[4] = 1;                      // version del formato
    fichero[5] = (u8) PLANETAS;
    fichero[6] = (u8) GALAXIAS;
    fichero[7] = (u8) PRESET_COUNT;      // planetas por galaxia
    for (i = 0; i < PLANETAS; i++)
    {
        const unsigned v = offPlaneta[i] + CABECERA;

        fichero[8 + (i * 4) + 0] = (u8) (v >> 24);
        fichero[8 + (i * 4) + 1] = (u8) (v >> 16);
        fichero[8 + (i * 4) + 2] = (u8) (v >> 8);
        fichero[8 + (i * 4) + 3] = (u8) v;
    }
    memcpy(&fichero[CABECERA], horno, hornoLen);

    return CABECERA + hornoLen;
}

// --- LA VUELTA DEL PLANETA CON EL CARGADOR DE VERDAD ---
//
// Vuelve a generar el planeta vivo, se lo guarda, llama a GuideMap_load (el de
// guidemap.c, el que va en el cartucho) sobre los bytes horneados y compara
// guideMap celda a celda. Es la red del paso 4: si el cargador del planeta y
// el generador no dejan lo mismo, salta aqui y no jugando.
static MapCell vivo[MAX_MAP_ROWS][MAX_MAP_COLS];
static int guiaMal;

static void compruebaGuia(int indice, int preset, u16 semilla)
{
    const Preset *p = &PRESETS[preset];
    u8 vCols, vRows, vLetras, vIniC, vIniR, vMetaC, vMetaR;
    u8 vEnlC, vEnlR, vEnlD, vEnlO;
    u8 vLetraC[ITEM_COUNT], vLetraR[ITEM_COUNT];
    u8 col, row;
    int i;

    mapCols = p->cols;
    mapRows = p->rows;
    itemCount = p->letras;
    mapSeed = semilla;
    setRandomSeed(mapSeed);
    Maze_setRoomSize(p->roomW, p->roomH, p->variedad);
    GuideMap_setShape(p->mapa, p->cols, p->rows);
    GuideMap_generate();

    memcpy(vivo, guideMap, sizeof vivo);
    vCols = mapCols; vRows = mapRows; vLetras = itemCount;
    vIniC = startCol; vIniR = startRow; vMetaC = goalCol; vMetaR = goalRow;
    vEnlC = insertLinkCol; vEnlR = insertLinkRow;
    vEnlD = insertLinkDir; vEnlO = insertLinkOffset;
    for (i = 0; i < ITEM_COUNT; i++) { vLetraC[i] = itemCol[i]; vLetraR[i] = itemRow[i]; }

    GuideMap_load(fichero, Levels_planet(fichero, (u8) indice));

    if ((mapCols != vCols) || (mapRows != vRows) || (itemCount != vLetras) ||
        (startCol != vIniC) || (startRow != vIniR) ||
        (goalCol != vMetaC) || (goalRow != vMetaR) ||
        (insertLinkCol != vEnlC) || (insertLinkRow != vEnlR) ||
        (insertLinkDir != vEnlD) || (insertLinkOffset != vEnlO))
    {
        guiaMal++;
        printf("GUIA p%d meta: %u/%u %u/%u %u/%u ini %u,%u/%u,%u meta %u,%u/%u,%u enl %u,%u,%u,%u/%u,%u,%u,%u\n",
               preset, mapCols, vCols, mapRows, vRows, itemCount, vLetras,
               startCol, startRow, vIniC, vIniR, goalCol, goalRow, vMetaC, vMetaR,
               insertLinkCol, insertLinkRow, insertLinkDir, insertLinkOffset,
               vEnlC, vEnlR, vEnlD, vEnlO);
    }
    // Solo las letras que el planeta usa de verdad: el generador no limpia las
    // ranuras de mas (itemCount puede bajar en un mapa pequeno) y nadie las
    // lee -- todos los lectores paran en itemCount, items.c el primero.
    for (i = 0; i < vLetras; i++)
        if ((itemCol[i] != vLetraC[i]) || (itemRow[i] != vLetraR[i]))
        {
            guiaMal++;
            printf("GUIA p%d letra %d: %u,%u cargada contra %u,%u viva\n",
                   preset, i, itemCol[i], itemRow[i], vLetraC[i], vLetraR[i]);
        }

    for (row = 0; row < vRows; row++)
        for (col = 0; col < vCols; col++)
        {
            const MapCell *a = &vivo[row][col];
            const MapCell *b = &guideMap[row][col];
            int d;

            if ((a->type != b->type) || (a->visited != b->visited) ||
                (a->enemyKilled != b->enemyKilled))
            {
                guiaMal++;
                printf("GUIA p%d %u,%u tipo %u/%u\n", preset, col, row, b->type, a->type);
            }
            if (a->type != CELL_ROOM)
                continue;
            if ((a->doorN != b->doorN) || (a->doorE != b->doorE) ||
                (a->doorS != b->doorS) || (a->doorW != b->doorW))
            {
                guiaMal++;
                printf("GUIA p%d %u,%u puertas %d%d%d%d cargadas contra %d%d%d%d vivas\n",
                       preset, col, row, b->doorN, b->doorE, b->doorS, b->doorW,
                       a->doorN, a->doorE, a->doorS, a->doorW);
                continue;
            }
            for (d = 0; d < 4; d++)
                if (GuideMap_hasDoor(col, row, (u8) d))
                {
                    const u8 ov = (d == DOOR_N) ? a->doorOffsetN
                                : (d == DOOR_E) ? a->doorOffsetE
                                : (d == DOOR_S) ? a->doorOffsetS : a->doorOffsetW;

                    if (GuideMap_doorOffset(col, row, (u8) d) != ov)
                    {
                        guiaMal++;
                        printf("GUIA p%d %u,%u dir %d offset %u cargado contra %u vivo\n",
                               preset, col, row, d, GuideMap_doorOffset(col, row, (u8) d), ov);
                    }
                }
        }
}

static int escribeFichero(const char *ruta)
{
    FILE *f = fopen(ruta, "wb");
    const unsigned n = montaFichero();

    if (!f)
        return 0;

    fwrite(fichero, 1, n, f);
    fclose(f);
    return (int) n;
}


int main(int argc, char **argv)
{
    int i, total = 0;
    const int verboso = (argc > 1) && (strcmp(argv[1], "-v") == 0);

    hashes = (argc > 1) && (strcmp(argv[1], "-h") == 0);

    if ((argc > 1) && (strcmp(argv[1], "-f") == 0))
    {
        reparto((argc > 2) ? atoi(argv[2]) : 100);
        return 0;
    }

    if (!hashes)
        printf("galaxia preset semilla rejilla salas piso probadas\n");
    {
        u16 rodante = 0x1234;
        int g;

        for (g = 0; g < GALAXIAS; g++)
            for (i = 0; i < PRESET_COUNT; i++)
            {
                const int indice = (g * PRESET_COUNT) + i;
                Calidad c;
                int piso, probadas, n;
                u16 sem;

                sem = eligeSemilla(i, rodante, &c, &piso, &probadas);
                rodante = (u16) (sem + 7919);
                semillaDe[indice] = sem;

                n = planeta(indice, i, sem, verboso && (indice == 2));
                total += n;

                if (hashes)
                    printf("CMP PLANETA %d salas %d M%04X V%04X P%04X\n", indice, n,
                           totalM, totalV, totalP);
                else
                    printf("   %d      %d    %04X    %2ux%-2u   %3d   %3d   %5d\n",
                           g, i, sem, PRESETS[i].cols, PRESETS[i].rows, n, piso, probadas);
            }
    }
    if (!hashes)
    {
        printf("\n%d salas en %d planetas (%d galaxias de %d), %d plantas"
               " (%.1f de media por sala, maximo %d)\n",
               total, PLANETAS, GALAXIAS, PRESET_COUNT, plantasTotal,
               (double) plantasTotal / total, plantasMax);
        printf("\nHORNO: %d registros de sala (las %d de rejilla mas una de extraccion por planeta)\n",
               salasHorneadas, total);
        printf("       salas %u bytes (%.1f cada una), planetas %u bytes (%.1f cada uno)\n",
               bytesSalas, (double) bytesSalas / salasHorneadas,
               bytesPlanetas, (double) bytesPlanetas / PLANETAS);
        montaFichero();
        for (i = 0; i < PLANETAS; i++)
            compruebaGuia(i, i % PRESET_COUNT, semillaDe[i]);
        printf("CARGADOR REAL (Maze_loadRoom + Plants_loadForRoom): %d diferencias\n",
               cargadorMal);
        printf("CARGADOR DEL PLANETA (GuideMap_load): %d diferencias\n", guiaMal);
        printf("IDA Y VUELTA: metadatos mal %d, celdas mal %d, plantas mal %d, planetas mal %d\n",
               metaMal, celdasMal, plantasMal_total(), planetasMal);
        {
            const int n = escribeFichero("res/levels.bin");

            printf("\nres/levels.bin: %d bytes (cabecera %d + payload %u) = %.1f KB\n",
                   n, (int) CABECERA, hornoLen, n / 1024.0);
        }
    }
    return 0;
}
