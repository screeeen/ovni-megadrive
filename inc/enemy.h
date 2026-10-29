#ifndef _ENEMY_H_
#define _ENEMY_H_

#include <genesis.h>

// Varios enemigos por sala y de especies distintas (peticion del usuario:
// "muchos mas enemigos distintos de forma que el gameplay sea
// interesante"). Cada especie tiene su propia forma de moverse, su propia
// velocidad y su propio reparto entre peligrosa y vulnerable; cuantos hay
// y cuantas especies se mezclan lo decide el planeta (main.c,
// enemiesPerRoom/enemySpeciesCap).
//
// Lo que NO cambia respecto al enemigo unico de antes: donde aparecen es
// determinista por roomSeed (la misma sala siempre trae los mismos), la
// fase del parpadeo sale del random() compartido (la sala no te recibe
// igual dos veces), ninguno aparece en la trayectoria de entrada o salida
// de una puerta, y ninguno vuelve si lo mataste.
//
// El orden del enum es el de los fotogramas de enemyShip
// (tools/enemy_tiles.py), que ahora van de cinco en cinco: por cada
// (2*especie + vulnerable) hay la postura normal y las cuatro estrujado
// contra el muro que acaba de golpear. La forma es la senal de estado, el
// color solo acompana.
typedef enum
{
    ESP_PATRULLA,   // carril recto pegado a un muro, rebota -- el de siempre
    ESP_TORRETA,    // quieta, casi siempre peligrosa: un obstaculo con ventana
    ESP_RONDA,      // da la vuelta al perimetro interior, gira en las esquinas
    ESP_ACECHO,     // avanza una celda hacia la nave cada vez que la nave se para
    ESP_DESLIZA,    // se desliza hasta el muro como la nave; vulnerable solo al pararse
    ESP_ESPECTRO,   // atraviesa muros, se reorienta hacia la nave, casi siempre vulnerable
    ENEMY_SPECIES_COUNT
} EnemySpecies;

// Cuantos caben en una sala: la mascara de muertos de guidemap es un u8.
#define ENEMIES_MAX 6

typedef enum { ENEMY_DANGEROUS, ENEMY_VULNERABLE } EnemyState;

// Parpadeo de muerte (marcador de posicion, sin arte propio).
#define ENEMY_DEATH_BLINK_FRAMES 24
#define ENEMY_DEATH_BLINK_PERIOD 4

// Lo que dura el estrujado contra un muro (peticion del usuario: "todos los
// enemigos que choquen contra pared tengan su animacion respectiva de squeeze
// como el player"). Los mismos 10 frames que la nave -- main.c los tiene
// atados al temblor de pantalla para que golpe, temblor y deformacion lean
// como un solo suceso, y aqui no hay temblor pero el gesto se reconoce igual.
#define ENEMY_SQUASH_FRAMES 10

typedef struct
{
    s16 x;              // esquina de la caja de una celda, como Player
    s16 y;
    u8 species;         // EnemySpecies
    u8 dir;             // DIR_UP/DOWN/LEFT/RIGHT (player.h)
    u8 state;           // EnemyState
    u8 tick;            // divisor de velocidad, para las especies lentas
    u8 glide;           // px que le quedan al paso de celda en curso (ESP_ACECHO)
    u8 pause;           // fotogramas parado entre deslizamientos (ESP_DESLIZA)
    u8 squashDir;       // contra que muro se estrujo, y cuanto le queda
    u8 squashLeft;
    u8 easeIdx;         // el paso del ease, con un sentido por especie: el
                        // tramo del paso de celda (ACECHO), lo acelerado que
                        // va (DESLIZA) o por donde va el vaiven (ESPECTRO)
    bool sawRest;       // la nave estaba parada el frame anterior (ESP_ACECHO)
    bool alive;
    u16 stateTimer;
    u16 rng;            // su propio azar: no toca el random() compartido
    u16 aimTimer;       // cada cuanto se reorienta (ESP_ESPECTRO)
    u16 deathTimer;
} Enemy;

// Llena list[0..count-1] para esta sala. speciesCap es cuantas especies
// entran en el sorteo (las primeras del enum, main.c las abre por planeta);
// las que salen son consecutivas desde una base sacada de roomSeed, asi que
// mientras count <= speciesCap no se repite ninguna en la misma sala.
// killedMask lleva un bit por indice ya muerto (guidemap): esos quedan con
// alive FALSE y deathTimer 0 sin volver a aparecer. Un indice sin sitio
// legal queda igual, asi que main.c lo reconoce por no estar en killedMask
// y lo apunta como muerto para que la sala pueda completarse.
// doorN/E/S/W y doorOffsets[4] (0=N,1=E,2=S,3=W) son las puertas activas:
// de cada una se traza la recta por la que la nave entra y sale, y esas
// celdas quedan prohibidas para siempre -- no solo al aparecer, tambien
// hacen de muro para todas las especies, incluido el espectro. Son los
// pasillos seguros, y es lo que hace jugable una sala con seis.
void Enemy_spawnGroup(Enemy *list, u8 count, u8 speciesCap, u8 killedMask, u16 roomSeed,
                       bool doorN, bool doorE, bool doorS, bool doorW,
                       const u8 doorOffsets[4]);

// Un fotograma de uno. playerX/playerY es la esquina de la caja de la nave
// y playerResting si no se movio en el fotograma anterior: las dos las usan
// el acecho y el espectro para apuntar.
void Enemy_update(Enemy *e, s16 playerX, s16 playerY, bool playerResting);

void Enemy_kill(Enemy *e);
bool Enemy_isGone(const Enemy *e);
bool Enemy_blinkVisible(const Enemy *e);
bool Enemy_overlapsBox(const Enemy *e, s16 boxX, s16 boxY);

// Fotograma de enemyShip que le toca: forma de la especie, estado y postura
// (normal, o estrujado contra el muro que acaba de golpear).
u8 Enemy_frame(const Enemy *e);

// Tres letras, para la vista de datos.
const char *Enemy_speciesTag(u8 species);

#endif
