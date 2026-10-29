#ifndef _PLANETART_H_
#define _PLANETART_H_

#include <genesis.h>

// LOS PLANETAS DEL MENU, dibujados en la consola y no sacados de un PNG.
//
// No hay dibujos: hay CAPAS que se superponen (peticion del usuario: "quiero
// distintos patrones graficos que se interpongan entre si, se combinen entre
// si y eso genere nuevos planetas"). Cada planeta apila una o dos capas de
// superficie sobre una silueta, le pone o no anillo y luna, y elige una de
// tres rampas de tinta.
//
//     3 siluetas x 16 mezclas de capas x 2 lunas x 3 rampas  = 288 por galaxia
//     x 5 anillos x 8 tonos                                  = 11520 planetas
//
// QUE ES DE LA GALAXIA Y QUE DEL PLANETA. La galaxia se queda con el anillo y
// la familia de tono, que es lo que hace que al cambiar con ARRIBA/ABAJO se
// vea que has cambiado de sitio. Todo lo demas es del planeta, para que dentro
// de una misma galaxia no haya dos iguales. El usuario es daltonico, asi que
// la galaxia NO se reconoce por el color: se reconoce por el anillo y por como
// giran sus orbitas (sentido y achatamiento, aqui abajo).

// Las tintas: TRES rampas de cuatro (sombra, base, luz, detalle), en indices
// de paleta. El 0 es transparente y el 1 se lo queda el menu para su violeta
// de siempre, asi que empiezan en el 2 y llegan al 13.
#define PLANET_PAL_FIRST   2
#define PLANET_RAMP_LEVELS 4
#define PLANET_RAMPS       3
#define PLANET_PAL_COUNT   (PLANET_RAMPS * PLANET_RAMP_LEVELS)   // 12

// El color VDP de (rampa 0..2, nivel 0..3) de esa galaxia. Las tres rampas son
// variaciones del tono de la familia, no tonos sueltos: dentro de una galaxia
// los planetas se distinguen, pero siguen siendo de la misma.
u16 PlanetArt_tint(u8 galaxia, u8 rampa, u8 nivel);

// Dibuja el planeta `planeta` (0..7) de `galaxia` en un lienzo cuadrado de
// `lado` px (8, 16 o 24), un byte por pixel: 0 transparente, o un indice entre
// PLANET_PAL_FIRST y PLANET_PAL_FIRST + PLANET_PAL_COUNT - 1.
void PlanetArt_draw(u8 galaxia, u8 planeta, u8 lado, u8 *px);

// Cuanto se estira la orbita de esa galaxia, en centesimas, y hacia donde gira
// (+1 o -1). Junto con el anillo, es lo que identifica una galaxia sin usar el
// color: las cinco formas de anillo por los dos sentidos dan diez firmas para
// ocho galaxias.
s16 PlanetArt_orbitStretch(u8 galaxia);
s16 PlanetArt_orbitDir(u8 galaxia);
s16 PlanetArt_orbitSpeed(u8 galaxia);   // tambien en centesimas

#endif
