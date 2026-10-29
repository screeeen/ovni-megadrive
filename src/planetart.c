#include "planetart.h"

// --- LOS EJES ---

typedef enum { REDONDO, ACHATADO, HUEVO } Silueta;
typedef enum { SIN_ANILLO, ANILLO_FINO, ANILLO_ANCHO, ANILLO_DOBLE, ANILLO_POLAR } Anillo;

// LAS CAPAS DE SUPERFICIE, que se SUPERPONEN: cada una repinta lo que la
// anterior dejo, asi que dos capas juntas no son ninguna de las dos.
#define CAPA_BANDAS    0x01
#define CAPA_MOTEADO   0x02
#define CAPA_CASQUETES 0x04
#define CAPA_REMOLINO  0x08
#define CAPA_CRATERES  0x10
#define CAPA_FASE      0x20   // la media luna; va la ultima, se come todo

// Las mezclas que valen. Una tabla y no los 64 valores de la mascara porque
// hay combinaciones que a 24 px no son nada: tres capas encima dan ruido y
// pierdes el planeta. Una o dos, elegidas a ojo.
#define MEZCLA_COUNT 16
static const u8 MEZCLAS[MEZCLA_COUNT] = {
    CAPA_BANDAS,
    CAPA_MOTEADO,
    CAPA_CASQUETES,
    CAPA_REMOLINO,
    CAPA_CRATERES,
    (u8) (CAPA_BANDAS | CAPA_CASQUETES),
    (u8) (CAPA_MOTEADO | CAPA_CASQUETES),
    (u8) (CAPA_BANDAS | CAPA_FASE),
    (u8) (CAPA_MOTEADO | CAPA_FASE),
    (u8) (CAPA_REMOLINO | CAPA_CASQUETES),
    (u8) (CAPA_CRATERES | CAPA_CASQUETES),
    (u8) (CAPA_BANDAS | CAPA_MOTEADO),
    (u8) (CAPA_REMOLINO | CAPA_FASE),
    (u8) (CAPA_CRATERES | CAPA_FASE),
    (u8) (CAPA_BANDAS | CAPA_REMOLINO),
    (u8) (CAPA_MOTEADO | CAPA_REMOLINO),
};

// El anillo es de la GALAXIA. Cinco formas para ocho galaxias, asi que dos
// comparten anillo -- las separa el sentido de giro de sus orbitas, que no
// necesita color para leerse.
static const u8 FAMILIA_ANILLO[8] = {
    SIN_ANILLO, ANILLO_FINO, ANILLO_ANCHO, ANILLO_DOBLE,
    ANILLO_POLAR, SIN_ANILLO, ANILLO_FINO, ANILLO_DOBLE
};

// El tono base de cada familia, en niveles 0..7 por canal, que es lo que el
// VDP tiene de verdad. Escribirlos en 24 bits y dejar que RGB24_TO_VDPCOLOR
// los trunque es como se cuelan tonos que no existen.
static const u8 TONO[8][3] = {
    { 5, 1, 6 },   // 0 violeta
    { 1, 5, 2 },   // 1 verde
    { 6, 3, 0 },   // 2 ambar
    { 2, 5, 7 },   // 3 azul hielo
    { 6, 1, 1 },   // 4 rojo
    { 1, 6, 5 },   // 5 turquesa
    { 7, 4, 0 },   // 6 naranja
    { 3, 4, 6 },   // 7 gris azulado
};

typedef struct
{
    u8 silueta;
    u8 anillo;
    u8 capas;
    u8 luna;
    u8 rampa;
} Firma;

static u8 pinza(s16 v) { return (u8) ((v < 0) ? 0 : ((v > 7) ? 7 : v)); }

// La firma de (galaxia, planeta). La galaxia pone el anillo; el planeta pone
// silueta, capas, luna y rampa, con pasos primos entre si para que los ocho de
// una galaxia recorran cosas distintas y para que el mismo tamano se vea
// distinto en otra galaxia.
static void firmaDe(u8 galaxia, u8 planeta, Firma *out)
{
    const u8 g = (u8) (galaxia & 7);
    const u8 p = (u8) (planeta & 7);

    out->anillo = FAMILIA_ANILLO[g];
    out->silueta = (u8) (((p * 2) + g) % 3);
    out->capas = MEZCLAS[((p * 5) + (g * 3)) % MEZCLA_COUNT];
    out->luna = (u8) (((p * 3) + g) & 1);
    out->rampa = (u8) (((p * 7) + (g * 2)) % PLANET_RAMPS);
}

// LAS TRES RAMPAS de una galaxia: tres tonos ENTEROS de la tabla, no mezclas.
// Mezclar un tono con el de al lado lo desatura -- el ambar mezclado con el
// azul da un beige sucio que luego la luz convierte en blanco --, asi que cada
// rampa es un tono de pleno derecho. Los pasos de 3 y 5 son primos con 8, asi
// que el trio de una galaxia no se repite en ninguna otra.
static void tonoDeRampa(u8 galaxia, u8 rampa, u8 *out)
{
    static const u8 SALTO[PLANET_RAMPS] = { 0, 3, 5 };
    const u8 g = (u8) ((galaxia + SALTO[rampa % PLANET_RAMPS]) & 7);
    u8 c;

    for (c = 0; c < 3; c++)
        out[c] = TONO[g][c];
}

u16 PlanetArt_tint(u8 galaxia, u8 rampa, u8 nivel)
{
    u8 base[3];
    u8 ch[3];
    u8 c;

    tonoDeRampa(galaxia, rampa, base);

    for (c = 0; c < 3; c++)
    {
        const s16 v = base[c];

        switch (nivel)
        {
            case 0:  ch[c] = pinza((s16) ((v * 2) / 5)); break;              // sombra
            case 1:  ch[c] = pinza(v); break;                                // base
            case 2:  ch[c] = pinza((s16) (v + (((7 - v) * 2) / 5))); break;  // luz
            default: ch[c] = pinza((s16) (v + (((7 - v) * 3) / 4))); break;  // detalle
        }
    }

    return RGB3_3_3_TO_VDPCOLOR(ch[0], ch[1], ch[2]);
}

s16 PlanetArt_orbitDir(u8 galaxia)
{
    // Alternas: la mitad de las galaxias giran al contrario, que es lo que se
    // nota antes de todo cuando cambias con ARRIBA/ABAJO.
    return (galaxia & 1) ? -1 : 1;
}

s16 PlanetArt_orbitStretch(u8 galaxia)
{
    // Cuanto se achata la elipse. 100 es la de siempre.
    static const s16 v[8] = { 100, 72, 128, 58, 112, 88, 142, 66 };

    return v[galaxia & 7];
}

s16 PlanetArt_orbitSpeed(u8 galaxia)
{
    static const s16 v[8] = { 100, 118, 86, 130, 94, 108, 78, 122 };

    return v[galaxia & 7];
}

// --- EL DIBUJO ---
//
// Todo con enteros: este generador corre en la consola cada vez que se cambia
// de galaxia, no en una herramienta de escritorio.

static void pon(u8 *px, u8 lado, s16 x, s16 y, u8 tinta)
{
    if ((x < 0) || (y < 0) || (x >= lado) || (y >= lado))
        return;

    px[(y * lado) + x] = tinta;
}


// Ruido estable: el moteado de un planeta tiene que ser SIEMPRE el mismo, asi
// que sale de (x, y, galaxia) y no de random().
static u16 ruido(u8 galaxia, u8 planeta, s16 x, s16 y)
{
    u16 h = (u16) (galaxia * 2654u + planeta * 40503u);

    h ^= (u16) ((x + 7) * 2749u);
    h ^= (u16) ((y + 13) * 7919u);
    h ^= (u16) (h >> 7);
    h = (u16) (h * 25173u + 13849u);

    return (u16) (h >> 5);
}

static s16 raizEntera(s32 v)
{
    s16 r = 0;

    while (((s32) (r + 1) * (r + 1)) <= v)
        r++;

    return r;
}

// EL ANILLO, recorriendo su eje LARGO y calculando el corto. La forma
// implicita (x/rx)^2+(y/ry)^2~1 con un margen daba una banda gorda en el
// centro y fina en las puntas -- o sea, una raya cruzando la cara del planeta
// en vez de un anillo. Asi sale una elipse de grosor constante.
//
// `mitad` dice que media elipse se pinta: -1 la de detras del planeta, +1 la
// de delante. `polar` la pone de canto (ejes cambiados).
static void dibujaAnillo(u8 *px, u8 lado, s16 cx, s16 cy, s16 rx, s16 ry,
                         s16 grosor, s16 mitad, bool polar, u8 tinta)
{
    const s16 largo = polar ? ry : rx;
    const s16 corto = polar ? rx : ry;
    s16 anterior = -1;
    s16 t;

    if ((largo <= 0) || (corto <= 0))
        return;

    for (t = (s16) -largo; t <= largo; t++)
    {
        const s32 resto = ((s32) largo * largo) - ((s32) t * t);
        const s16 c = raizEntera(((s32) corto * corto * resto) / ((s32) largo * largo));
        // DE DONDE A DONDE SE PINTA ESTA COLUMNA. Pintar solo `c` dejaba la
        // elipse rota: cerca de las puntas la curva baja mas de un pixel de
        // una columna a la siguiente, y el contorno salia a trozos. Se rellena
        // desde donde quedo la columna anterior, que es lo que la cierra.
        const s16 alto = (anterior < 0) ? c : ((c > anterior) ? c : anterior);
        const s16 bajo = (s16) (((anterior < 0) ? c : ((c < anterior) ? c : anterior))
                                - (grosor - 1));
        s16 d;

        anterior = c;

        for (d = (bajo < 0) ? 0 : bajo; d <= alto; d++)
        {
            if (polar)
            {
                const s16 y = (s16) (cy + t);

                if (mitad > 0)
                    pon(px, lado, (s16) (cx + d), y, tinta);
                else
                    pon(px, lado, (s16) (cx - d), y, tinta);
            }
            else
            {
                const s16 x = (s16) (cx + t);

                if (mitad > 0)
                    pon(px, lado, x, (s16) (cy + d), tinta);
                else
                    pon(px, lado, x, (s16) (cy - d), tinta);
            }
        }
    }
}

// EL DISCO SE ENCOGE CUANDO HAY ANILLO, y bastante: si el anillo no es
// claramente mas ancho que el planeta, las dos puntas se pegan al borde del
// disco y lo que se ve es una lente, no un planeta con anillo. A cinco
// octavos de la caja el anillo asoma lo suyo por los dos lados.
static s16 semiejeConAnillo(s16 med)
{
    const s16 a = (s16) ((med * 5) / 8);

    return (a < 2) ? 2 : a;
}

// Los cuatro niveles de una rampa. El indice de paleta sale de juntar la
// rampa del planeta con el nivel: por eso el dibujo trabaja en niveles y solo
// al escribir el pixel se convierte.
#define NIVEL_SOMBRA  0
#define NIVEL_BASE    1
#define NIVEL_LUZ     2
#define NIVEL_DETALLE 3

static u8 tintaDe(const Firma *f, u8 nivel)
{
    return (u8) (PLANET_PAL_FIRST + (f->rampa * PLANET_RAMP_LEVELS) + nivel);
}

void PlanetArt_draw(u8 galaxia, u8 planeta, u8 lado, u8 *px)
{
    Firma firma;
    const s16 med = (s16) (lado / 2);
    const s16 cx = (s16) ((lado - 1) / 2);
    const s16 cy = cx;
    // Centro en medios pixeles: el lienzo es par, asi que el centro cae entre
    // dos pixeles y dx = x*2 - (lado-1) deja el disco simetrico.
    const s16 cx2 = (s16) (lado - 1);
    bool conAnillo, polar;
    s16 a, b, anRx, anRy, anRxPolar, grosor;
    u8 tintaAnillo;
    s16 x, y;

    firmaDe(galaxia, planeta, &firma);

    conAnillo = (firma.anillo != SIN_ANILLO);
    polar = (firma.anillo == ANILLO_POLAR);
    tintaAnillo = tintaDe(&firma, NIVEL_DETALLE);
    a = conAnillo ? semiejeConAnillo(med) : med;   // semieje horizontal, px
    b = a;                                         // semieje vertical, px
    anRx = (s16) (med - 1);
    // Un quinto del ancho era DEMASIADO PLANO: la raiz entera cuantizaba la
    // elipse en tres escalones y el mas largo salian diecisiete pixeles
    // seguidos, o sea una barra cruzando el planeta. A un tercio la curva
    // tiene escalones de sobra y se lee como un anillo.
    // La altura del anillo varia un pixel segun el planeta: dos anillos del
    // mismo tipo no se ven igual de inclinados, que es la otra mitad de lo
    // mismo.
    anRy = (s16) (polar ? (med - 1)
                        : ((med / 3) + 1 - ((lado >= 16) ? (planeta & 1) : 0)));
    // De canto el anillo es mas estrecho que el planeta y mas alto: asi se ven
    // los dos arcos asomando por los polos y la linea cruzandole la cara.
    anRxPolar = (s16) (med / 2);
    grosor = (s16) ((firma.anillo == ANILLO_ANCHO) ? ((lado >= 16) ? 3 : 2) : 1);

    // ACHATADO no se junta con anillo: el disco ya viene encogido para dejarle
    // sitio, y achatarlo encima deja una rendija dentro de un aro, que lee
    // como un ojo. Con anillo, el achatado se queda en redondo.
    switch (firma.silueta)
    {
        // Cinco octavos en los grandes sin anillo. Con anillo, o a 8 px, tres
        // cuartos: a cinco octavos sobre un disco ya encogido para dejarle
        // sitio al aro queda una rendija que lee como un ojo. Antes el
        // achatado DESAPARECIA cuando habia anillo, y eso era lo que hacia que
        // los ocho planetas de una galaxia con anillo se parecieran entre si.
        case ACHATADO:
            b = (s16) ((conAnillo || (lado < 16)) ? ((a * 3) / 4) : ((a * 5) / 8));
            break;
        case HUEVO:    b = (s16) ((a * 7) / 8); break;
        default: break;                                 // REDONDO
    }
    if (b < 2) b = 2;

    for (y = 0; y < lado; y++)
        for (x = 0; x < lado; x++)
            px[(y * lado) + x] = 0;

    // 1. La mitad del anillo que va POR DETRAS: se pinta antes y el disco la tapa.
    if (conAnillo)
    {
        dibujaAnillo(px, lado, cx, cy, polar ? anRxPolar : anRx, anRy, grosor, -1, polar, tintaAnillo);
        if (firma.anillo == ANILLO_DOBLE)
            dibujaAnillo(px, lado, cx, cy, (s16) (anRx / 2), (s16) ((anRy / 2) + 1),
                         grosor, -1, polar, tintaAnillo);
    }

    // 2. El disco: sombreado, y encima las capas de superficie, una sobre otra.
    for (y = 0; y < lado; y++)
        for (x = 0; x < lado; x++)
        {
            const s16 dx = (s16) ((x * 2) - cx2);
            const s16 dy = (s16) ((y * 2) - cx2);
            // DE HUEVO: estrecho arriba y ancho abajo, asimetrico de verdad.
            const s16 bulto = (s16) ((firma.silueta == HUEVO)
                                     ? ((lado >= 24) ? 2 : ((lado >= 16) ? 1 : 0)) : 0);
            const s16 ae = (s16) (a + ((dy > 0) ? bulto : -bulto));
            const s32 dentro = ((s32) dx * dx * b * b) + ((s32) dy * dy * ae * ae);
            const s32 radio = (s32) ae * ae * b * b * 4;   // *4: dx,dy van en medios pixeles
            u8 nivel;

            if (dentro > radio)
                continue;

            // LA LUZ ES RADIAL, no una suma de dx y dy. Con la suma, las dos
            // fronteras entre luz, base y sombra salen RECTAS y en diagonal, y
            // lo que se ve es un diamante facetado, no una esfera. Midiendo la
            // distancia a un punto de luz las fronteras salen curvas, que es
            // lo que hace que una bola parezca una bola.
            {
                const s32 lx = ((s32) dx * 5) + ((s32) ae * 6);   // luz arriba a la izquierda
                const s32 ly = ((s32) dy * 5) + ((s32) b * 6);
                const s32 d2 = (lx * lx) + (ly * ly);
                const s32 esc = ((s32) ae * ae) + ((s32) b * b);

                nivel = (u8) ((d2 < (esc * 36)) ? NIVEL_LUZ
                            : ((d2 > (esc * 144)) ? NIVEL_SOMBRA : NIVEL_BASE));
            }

            // --- LAS CAPAS, en orden. Cada una repinta lo que dejo la anterior.
            if (firma.capas & CAPA_BANDAS)
            {
                if ((((y / 2) & 1) == 0) && (nivel != NIVEL_LUZ))
                    nivel = (u8) ((nivel == NIVEL_SOMBRA) ? NIVEL_BASE : NIVEL_LUZ);
            }
            if (firma.capas & CAPA_MOTEADO)
            {
                if ((ruido(galaxia, planeta, x, y) & 3) == 0)
                    nivel = (u8) ((nivel == NIVEL_SOMBRA) ? NIVEL_BASE : NIVEL_SOMBRA);
            }
            if (firma.capas & CAPA_REMOLINO)
            {
                if ((((dx + (dy * 3)) / 4) & 3) == 0)
                    nivel = NIVEL_DETALLE;
            }
            if (firma.capas & CAPA_CRATERES)
            {
                // Hoyos: un anillo claro con el fondo oscuro, sembrados por el
                // mismo ruido estable. Se nota sobre todo en los de 24.
                const u16 n = ruido(galaxia, planeta, (s16) (x / 3), (s16) (y / 3));

                if ((n & 7) == 0)
                    nivel = NIVEL_LUZ;
                else if ((n & 7) == 1)
                    nivel = NIVEL_SOMBRA;
            }
            if (firma.capas & CAPA_CASQUETES)
            {
                // dy va en medios pixeles y b en enteros: sin el *2 la
                // condicion no se cumple nunca y los casquetes no existen.
                if ((dy < (s16) ((-b * 2 * 3) / 5)) || (dy > (s16) ((b * 2 * 3) / 5)))
                    nivel = NIVEL_LUZ;
            }
            if (firma.capas & CAPA_FASE)
            {
                // La ultima: es una fase, no un sombreado, y se come lo demas.
                if (dx > (s16) (-ae / 2))
                    nivel = NIVEL_SOMBRA;
            }

            pon(px, lado, x, y, tintaDe(&firma, nivel));
        }

    // 3. La mitad del anillo que va DELANTE.
    if (conAnillo)
    {
        dibujaAnillo(px, lado, cx, cy, polar ? anRxPolar : anRx, anRy, grosor, 1, polar, tintaAnillo);
        if (firma.anillo == ANILLO_DOBLE)
            dibujaAnillo(px, lado, cx, cy, (s16) (anRx / 2), (s16) ((anRy / 2) + 1),
                         grosor, 1, polar, tintaAnillo);
    }

    // 4. La luna, en la esquina de abajo a la derecha y fuera del disco.
    if (firma.luna)
    {
        const s16 n = (s16) ((lado >= 16) ? 2 : 1);
        s16 i, j;

        for (j = 0; j < n; j++)
            for (i = 0; i < n; i++)
                pon(px, lado, (s16) (lado - n + i), (s16) (lado - n + j),
                    tintaDe(&firma, NIVEL_LUZ));
    }
}
