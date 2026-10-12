#include "sfx.h"
#include "resources.h"

static bool enabled = TRUE; // user request: "sonido es ON por defecto" (it used to start off)

// Rotating indices instead of a draw from the shared random() stream --
// that stream is reserved for the map/room/item/enemy generators' own
// determinism (same reasoning as main.c's shakeFramesLeft/triggerShake
// comment). Cycling 0->1->2->3->0... also guarantees the same variant
// never plays twice in a row, which a real random pick wouldn't.
//
// Las 4 variantes rotan sobre 3 canales (nextKickChannel va aparte, para no
// tener que dividir por 3): dos golpes consecutivos caen en canales distintos
// y se solapan en vez de cortarse. El cuarto canal es de los sonidos cortos.
static u8 nextKickVariant = 0;
static u8 nextKickChannel = 0;
// nextHihatVariant solo elige el sample: el hi-hat no tiene volumen por
// golpe, su canal es fijo (CH3) como el del resto de sonidos cortos.
static u8 nextHihatVariant = 0;

// Volume scaling (user request: "como el screenshake, el volumen que sea
// proporcional a la distancia") -- same shape as main.c's own
// triggerShake: a floor level (so even the shortest bump is audible) plus
// +1 per SFX_VOL_DIST_PER_LEVEL px of run-up, capped at the driver's max.
// Unlike the shake (capped at 3 of its own possible levels to keep the
// screen offset subtle), volume uses the PCM4 driver's full 0-15 range --
// there's no equivalent reason to hold this one back.
// El SUELO manda: casi todos los golpes son deslizamientos cortos, asi que el
// nivel que mas se oye en la partida es este y no el tope. A 6 se perdian 8 dB.
#define SFX_VOL_MIN            11
#define SFX_VOL_MAX            15
#define SFX_VOL_DIST_PER_LEVEL 16

// El canal de todo lo corto. Ver el reparto en la seccion del chordslop.
#define SFX_SHORT_CHANNEL SOUND_PCM_CH4

static void slopInit(void);

void Sfx_loadDriver(void)
{
    SND_PCM4_loadDriver(TRUE); // primero: deja el latch del YM en $2A, ver fmEnd
    slopInit();
}

void Sfx_setEnabled(bool value)
{
    enabled = value;
    if (!value)
        Sfx_slopStop(); // la C del menu calla tambien el acorde sostenido
}

bool Sfx_isEnabled(void)
{
    return enabled;
}

void Sfx_playWallHit(u16 distance)
{
    static const u8 *const kicks[4] = { kick0, kick1, kick2, kick3 };
    static const u32 kickLens[4] = { sizeof(kick0), sizeof(kick1), sizeof(kick2), sizeof(kick3) };
    const SoundPCMChannel channel = (SoundPCMChannel) nextKickChannel;
    u16 vol = SFX_VOL_MIN + (distance / SFX_VOL_DIST_PER_LEVEL);

    if (!enabled)
        return;

    if (vol > SFX_VOL_MAX)
        vol = SFX_VOL_MAX;

    // Set before starting playback so the very first mixed sample of this
    // hit already uses the new level, not whatever this channel was left
    // at by its previous hit.
    SND_PCM4_setVolume(channel, (u8) vol);
    SND_PCM4_startPlay(kicks[nextKickVariant], kickLens[nextKickVariant], channel, FALSE);
    nextKickVariant = (nextKickVariant + 1) & 3;
    nextKickChannel = (nextKickChannel == 2) ? 0 : (u8) (nextKickChannel + 1);
}

void Sfx_playPlantPickup(void)
{
    static const u8 *const hihats[4] = { hihat0, hihat1, hihat2, hihat3 };
    static const u32 hihatLens[4] = { sizeof(hihat0), sizeof(hihat1), sizeof(hihat2), sizeof(hihat3) };

    if (!enabled)
        return;

    SND_PCM4_setVolume(SFX_SHORT_CHANNEL, SFX_VOL_MAX);
    SND_PCM4_startPlay(hihats[nextHihatVariant], hihatLens[nextHihatVariant], SFX_SHORT_CHANNEL, FALSE);
    nextHihatVariant = (nextHihatVariant + 1) & 3;
}

// Placeholders -- see their doc comments in sfx.h. Synthesized at the
// same mono/16-bit/16kHz the other samples use:
//   enemyKill    a square tone sweeping 970 -> 70 Hz with a noise burst
//                over it, both decaying fast (~190 ms)
//   letterPickup two square blips a fifth apart, A5 then E6 (~160 ms)
#define SFX_VOL_TYPE_TICK 2 // lo mas bajo que sigue oyendose, ver sfx.h

void Sfx_playTypeTick(void)
{
    if (!enabled)
        return;

    // Canal fijo, no AUTO: asi un tick corta al anterior en vez de
    // apilarse con el. Si dos llegan muy seguidos se quiere uno, no dos
    // sonando a la vez.
    SND_PCM4_setVolume(SFX_SHORT_CHANNEL, SFX_VOL_TYPE_TICK);
    SND_PCM4_startPlay(typeTick, sizeof(typeTick), SFX_SHORT_CHANNEL, FALSE);
}

void Sfx_playEnemyKill(void)
{
    if (!enabled)
        return;

    SND_PCM4_setVolume(SFX_SHORT_CHANNEL, SFX_VOL_MAX);
    SND_PCM4_startPlay(enemyKill, sizeof(enemyKill), SFX_SHORT_CHANNEL, FALSE);
}

void Sfx_playLetterPickup(void)
{
    if (!enabled)
        return;

    SND_PCM4_setVolume(SFX_SHORT_CHANNEL, SFX_VOL_MAX);
    SND_PCM4_startPlay(letterPickup, sizeof(letterPickup), SFX_SHORT_CHANNEL, FALSE);
}


// --- Ambiente de fondo ------------------------------------------------------
// Lo que suena de fondo durante la partida. Hay dos modalidades y se elige con
// SLOP_MODE unas lineas mas abajo; esta puesta la sala de maquinas.
//
// LO QUE COMPARTEN, que es casi todo el codigo: cinco voces como maximo, cada
// una con su tono, su volumen y su deriva; rampas de volumen en software;
// deslizamiento de tono por una curva en S; crescendo por habitacion; y una
// sola rafaga de escrituras al chip por frame.
//
// SALA DE MAQUINAS. Peticion del usuario con una referencia concreta:
// "International Electromatics Headquarters Exterior", de Brian Hodgson para
// The Invasion (Doctor Who, 1968). Un cue de los que el Radiophonic Workshop
// hacia antes de tener sintetizadores: osciladores de prueba, ruido filtrado y
// bucles de cinta de longitudes distintas girando a la vez. Las resenas del
// disco lo describen como dieciseis minutos de zumbido y ruido blanco girando,
// disonante e incomodo. De ahi salen las cuatro decisiones:
//
//  - EL ZUMBIDO son dos A#1 separadas 18 centesimas, que baten entre si a 0,6
//    Hz. No es un acorde: es una sola nota que no se decide, que es lo que
//    hace un motor.
//  - LA DISONANCIA es un E3 flojo, a tritono del A#. Fija, nunca resuelve.
//  - EL CICLO es un golpe que cae de tono cada 47 frames. Es lo unico que se
//    oye como bucle y lo que la hace sonar a maquina y no a nube.
//  - EL SISEO QUE GIRA sale del PSG, y de una opcion suya que casi nadie usa:
//    el generador de ruido puede tomar su reloj del divisor del canal de tono
//    2 en vez de uno de los tres fijos. Eso hace el ruido blanco AFINABLE, y
//    barrer ese divisor es literalmente el barrido de la referencia.
//
// Y los cuatro bucles tienen periodos primos entre si (47, 157, 541 y 379
// frames), asi que la combinacion no vuelve a la misma posicion: es el truco
// de las cintas de longitudes distintas, que es como estaba hecho el original.
//
// MELODICO es el chordslop anterior, intacto: pad de dos voces con una
// progresion de ocho acordes en Do menor y una melodia encima con su propio
// ritmo. Su cabecera esta en su propio bloque.
//
// POR QUE FM Y UNA OCTAVA ABAJO, que se decidio cuatro veces:
//
//  1. PSG, porque es simple y no necesita el bus del Z80.
//  2. FM con cinco sinusoidales -- el usuario la oyo y la tiro: "muy feo y
//     rudo". Tenia razon. Una sinusoide pura es LO PEOR para un YM2612 de
//     Model 1, porque ese chip tiene el "ladder effect", una discontinuidad en
//     el cruce por cero de su DAC, y una sinusoide pasa mucho tiempo justo
//     ahi, con pendiente baja, dejando la distorsion al aire.
//  3. PSG otra vez, y afinado hasta su limite: su registro de tono es un
//     divisor de 10 bits, o sea un suelo de 109,3 Hz.
//  4. FM de nuevo, por un motivo distinto: el usuario pidio bajar dos octavas
//     y el PSG no puede. Se acordo una octava, que si se oye por un altavoz de
//     tele. Aqui no hay ninguna sinusoide, asi que lo de "feo y rudo" no es el
//     mismo experimento.
//
// Lo que se gana ademas de las notas graves: el tono tiene resolucion de 1 a 3
// centesimas por paso en vez de 1,8 a 8,1, y el volumen va en pasos de 0,75 dB
// en vez de 2.
//
// EL CONTRATO CON EL DRIVER PCM4, que es la parte que puede romper algo:
// drv_pcm4.s80 llama a initDAC al arrancar, y eso deja el latch de direccion
// de la parte 0 en $2A. Despues su bucle interno hace solo "LD (HL), A" sobre
// $4001 para siempre: escribe DATOS SIN DIRECCION, dando por hecho que el
// latch sigue donde lo dejo. Los canales FM 1 a 3 estan en esa misma parte 0,
// asi que cada escritura mia se lo mueve. Y YM2612_writeReg NO pide el bus.
// De ahi las dos reglas de slopFlush: tomar el bus mientras se escribe, y
// dejar el latch de vuelta en $2A antes de soltarlo. Sin lo segundo el driver
// manda las muestras del bombo a un registro cualquiera del YM2612.
//
// Los canales 4 y 5 estan en la parte 1, que no la toca nadie mas, asi que
// esas escrituras no tienen que reponer nada -- pero el bus igual.
//
// Por eso tambien se escribe lo menos posible: el Z80 tiene 223 ciclos por
// muestra a 16 kHz, asi que cada microsegundo con el bus tomado es una muestra
// que no sale. Solo se escribe lo que ha cambiado de verdad.
//
// DEL PSG, la sala de maquinas usa el canal de tono 2 (callado, solo como
// reloj del ruido) y el canal de ruido. Los canales 0 y 1 no se tocan.
//
// Con la sala en el MENU el reparto del canal de ruido con la estatica del
// visor de mapa ya no choca: el visor es una pantalla de partida y la partida
// suena melodica, que no toca el PSG. Sfx_ambNoise se queda igual porque no
// cuesta nada y vuelve a hacer falta el dia que esto se intercambie otra vez.

// DOS MODALIDADES. El motor (bus, rafaga de escrituras, rampas de volumen,
// deslizamiento de tono, deriva, crescendo) es el mismo para las dos; lo que
// cambia es el RELOJ, los TIMBRES y las notas:
//
//  MELODICO  LA PARTIDA. El chordslop: pad de dos voces con progresion de ocho
//            acordes y una melodia encima. Tres canales FM. Las tres suenan
//            desde la primera habitacion; lo que crece por cruce es el volumen.
//  MAQUINAS  EL MENU. Sala de maquinas, peticion del usuario con referencia
//            concreta: "International Electromatics Headquarters Exterior",
//            de Brian Hodgson para The Invasion (Doctor Who, 1968). Cinco
//            canales FM mas el ruido del PSG. Sin melodia y sin acordes:
//            zumbido batiente, disonancia fija, un golpe mecanico y un siseo
//            que barre, cada capa en un bucle de longitud distinta. Entra con
//            el zumbido y suma una capa por planeta que se selecciona.
//
// Las dos estan vivas a la vez y se cambia con Sfx_ambScene, que calla lo que
// suena, rehace timbre y tonos y deja el ambiente para entrar otra vez por
// capas. Quien decide la escena es main.c; aqui solo se obedece.
#define SLOP_MODE_MELODICO 0
#define SLOP_MODE_MAQUINAS 1

// Cinco voces es el maximo de las dos modalidades, y seis notas por voz. Las
// dos reservan el mismo sitio aunque la melodica solo use tres voces: asi los
// arrays de estado son unos y los mismos y no hay dos juegos de todo.
#define MEL_VOICES 3
#define MAQ_VOICES 5
#define SLOP_VOICES 5
#define SLOP_NOTES_MAX 6

// La modalidad viva, y cuantas voces usa. Lo cambia Sfx_ambScene, que rehace
// timbre y tonos; la eleccion es de main.c, no de aqui.
static u8 slopMode = SLOP_MODE_MAQUINAS;
static u8 slopVoices = MAQ_VOICES;

// --- LA MELODICA -----------------------------------------------------------

// Las dos graves son el PAD y la aguda es la MELODIA. Peticion del usuario:
// "hazlo mas melodico". Y lo melodico no sale de meter mas acordes: con las
// tres voces moviendose juntas cada ocho segundos lo que hay es armonia, no
// melodia. Hace falta una linea que se mueva por su cuenta, con su propio
// ritmo, por encima de un acompanamiento que se este quieto.
#define MEL_PAD_VOICES  2
#define MEL_MELODY_VOICE 2

// Las notas que puede tocar cada voz, en Hz y de grave a aguda. Todo sale de
// Do menor natural (C D Eb F G Ab Bb), la escala del export del usuario.
//
// Los tres registros NO se solapan, y eso es lo que hace que la melodia no
// pueda meterse nunca por debajo del pad: la interior llega como mucho a D4
// (294) y la melodia empieza en Eb4 (311). Asi no hay que comprobar cruces en
// tiempo de ejecucion, los hace imposibles el reparto.
//
// Las notas, en DECIMAS de Hz. Decimas y no Hz enteros porque aqui abajo la
// diferencia importa: A#1 es 58,27 y redondear a 58 ya son 8 centesimas.
//
// Es exactamente el reparto que tenia en el PSG una octava mas abajo, nota por
// nota. El FM llega de sobra: lo mas grave del chip son 0,1 Hz.
//
//     bajo      A#1 C2  D2  Eb2      58 - 78 Hz
//     interior  Eb2 F2  G2           78 - 98 Hz
//     melodia   A#2 C3  D3  Eb3 F3  117 - 175 Hz
//
// La fundamental del bajo (58 Hz) no la va a reproducir un altavoz de tele;
// lo que se oye de esa voz son sus armonicos impares, que el timbre de dos
// operadores pone en 174 y 290 Hz. Por eso el modulador del bajo va con mas
// indice que el de las otras voces: ahi no es un adorno, es lo audible.
//
// Eb2 esta en la paleta del bajo Y en la del interior: que no se cruce lo
// garantiza la tabla, que nunca pide ese par.
static const u16 melVoiceHz10[MEL_VOICES][SLOP_NOTES_MAX] = {
    {  583,  654,  734,  778,    0,    0 }, // A#1 C2  D2  Eb2      pad, bajo
    {  778,  873,  980,    0,    0,    0 }, // Eb2 F2  G2           pad, interior
    { 1165, 1308, 1468, 1556, 1746,    0 }, // A#2 C3  D3  Eb3 F3   melodia
};
static const u8 melVoiceCount[MEL_VOICES] = { 4, 3, 5 };

// --- Y LA SALA DE MAQUINAS -------------------------------------------------
//
// Cinco canales FM. Los tres primeros en la parte 0 (canales 1 a 3) y los dos
// ultimos en la parte 1 (canales 4 y 5); el 6 es del DAC y lo usa el driver
// PCM4 para el bombo.

#define MAQ_PAD_VOICES 3   // las tres que sostienen: el zumbido
#define SLOP_GOLPE_VOICE 3
#define SLOP_CLANC_VOICE 4

// Las notas, en decimas de Hz. Columna 0 la de reposo, columna 1 la de ataque
// (solo la usan las dos percusivas, que caen de tono al golpear).
//
// NO hay acorde. El zumbido son dos A#1 separadas 18 centesimas, que baten
// entre si a 0,6 Hz: eso es el motor. Encima, un E3 suelto, que contra un A#
// es un tritono -- la disonancia que hace que la sala no suene acogedora.
static const u16 maqVoiceHz10[MAQ_VOICES][2] = {
    {  583,  583 }, // A#1  58,3 Hz   el motor
    {  589,  589 }, // A#1 +18 cent   bate con el motor a 0,6 Hz
    { 1648, 1648 }, // E3  164,8 Hz   el tritono, flojito
    {  437,  655 }, // F1 -> C2       el golpe, cae de 65,5 a 43,7 Hz
    { 2330, 2330 }, // A#3 233,0 Hz   el clanc metalico
};
static const u8 maqVoiceCount[MAQ_VOICES] = { 1, 1, 1, 2, 1 };


// La S de sus curvas: logistica de k = 14,3, 33 puntos de 0 a 255.
static const u8 slopSigmoid[33] = {
      0,   0,   0,   1,   1,   2,   3,   4,   7,  11,  16,
     24,  36,  53,  74,  99, 127, 156, 181, 202, 219, 231,
    239, 244, 248, 251, 252, 253, 254, 254, 255, 255, 255,
};

// Volumen. Es el TL (Total Level) de la portadora: 7 bits, 0 lo mas fuerte y
// 127 silencio, 0,75 dB por paso. Va en el mismo sentido que la atenuacion del
// PSG que habia antes, asi que la logica de rampas no cambia de signo; lo que
// cambia es la escala, porque 0,75 dB por paso es 2,7 veces mas fino que los
// 2 dB del PSG. Todas las constantes de abajo estan en pasos de TL.
//
// NO se usa la envolvente del chip: AR al maximo y D1L/D1R a cero, o sea que
// la nota se sostiene para siempre, y el volumen lo lleva este codigo
// escribiendo TL. Es lo mismo que se hacia en el PSG (donde no habia otra),
// pero aqui es una eleccion: asi el crescendo, el soplo de cada nota y el
// desvanecido salen de un solo camino y con 0,75 dB de resolucion.
#define SLOP_TL_FLOOR   4   // el pad asentado
#define SLOP_TL_MELODY  0   // la melodia al techo del registro
#define SLOP_TL_JITTER  2   // 0..1, para que el pad no este cuadrado
#define SLOP_TL_STEP    2   // 1,5 dB por paso de rampa

// De donde arranca y donde acaba una rampa. 48 son -36 dB: inaudible con el
// acorde a -3, pero NO es el 127 del registro, y la diferencia importa --
// rampar desde 127 a pasos de 1,5 dB tardaria cinco segundos. Al nacer la voz
// se salta de 127 a 48 de golpe (no se oye) y de ahi baja la rampa.
#define SLOP_TL_QUIET  48
#define SLOP_TL_SILENT 127

// El crescendo (peticion del usuario: "que el volumen de las primeras notas
// sea mas bajo y que progresivamente sube cuando se van sumando
// instrumentos"). Dos cosas: las voces entran de una en una y encima hay una
// atenuacion extra que arranca en SLOP_FADE_START y baja hasta 0 por
// habitacion, deslizandose un paso cada SLOP_FADE_FRAMES frames. Afecta a
// todas las voces vivas, asi que crece el conjunto. Aqui son pocos pasos
// porque cada uno vale 2 dB, el doble que en FM.
// En pasos de TL: 6 son 4,5 dB, y con 3 por habitacion el conjunto esta
// arriba al entrar en la tercera.
#define SLOP_FADE_START      6
#define SLOP_FADE_PER_ROOM   3
#define SLOP_FADE_FRAMES     8

// Ataque y cola. Sin esto una voz APARECE en un frame, que sobre un acorde
// sostenido se oye como un golpe, y al morir se corta en seco. Un paso de
// SLOP_TL_STEP cada N frames; los numeros salen de igualar los dB por segundo
// que ya estaban probados en el PSG.
#define SLOP_ATTACK_FRAMES  5
#define SLOP_FADEOUT_FRAMES 3

// Movimiento continuo, en CENTESIMAS de semitono. SE QUITO EL VIBRATO RAPIDO:
// el registro de tono del PSG era un divisor y con aquel voicing un paso
// valia entre 2 y 3,6 centesimas, asi que un vibrato de +-10 centesimas eran
// cinco valores discretos -- un tremolo escalonado, no una onda. Era lo que
// mas aspereza metia en reposo. En FM el problema no existiria (1 a 3
// centesimas por paso), pero tampoco se ha echado de menos. Lo que queda es una deriva muy lenta y poco
// profunda: el registro cambia de paso en paso cada quince frames o mas, asi
// que el acorde respira sin rascar.
//
// Entre cruce y cruce el acorde esta practicamente quieto, y eso es
// deliberado: lo que se mueve de verdad son los cambios de habitacion.
#define SLOP_DRIFT_CENTS 8

// Velocidad: incremento de la fase de 8 bits por frame, o 60*rate/256 Hz en
// NTSC. A 1 son 0,23 Hz. Cada voz coge su fase al azar para que las tres no
// respiren a la vez.
#define SLOP_DRIFT_RATE_MIN 1
#define SLOP_DRIFT_RATE_MAX 2

// Cuanto tarda una voz en llegar a su nota nueva. SIN espera por delante: el
// deslizamiento arranca en el frame del cruce, que es lo que hace que el
// cambio se oiga como consecuencia de haber entrado por la puerta. Entre uno
// y dos segundos: suficiente para que no sea un salto y suficientemente corto
// para que se note.
#define SLOP_GLIDE_MIN  70
#define SLOP_GLIDE_MAX 130

// Tope del salto de tono por frame dentro de un bend, en centesimas. Sin esto
// un bend de 9 semitonos en una rampa corta daba saltos de casi dos semitonos
// de un frame al siguiente, que no se oye como un glissando sino como una
// escalera. La pendiente maxima de la sigmoide es k/4 = 3,6 veces la media,
// de ahi que la duracion minima de una rampa sea span*3,6/tope.
#define SLOP_BEND_CENTS_PER_FRAME 10

// Los parametros propios de la melodica.

// EL PULSO. Ocho segundos es el loopSec del export del Chord Slop Lab, asi que
// el compas sale de ahi: 16 pulsos de medio segundo. El acorde cambia en el
// primer pulso de cada compas y la melodia coloca sus notas en la rejilla de
// pulsos de dentro.
//
// Se quito el sorteo que antes desalineaba el cambio de acorde unos frames.
// Era para que no se oyera la rejilla, pero una rejilla es exactamente lo que
// le faltaba a esto para sonar a musica: ahora la variacion esta en la
// DURACION de las notas de la melodia, que es donde va el ritmo, y la armonia
// cae clavada en el compas.
//
// 30 frames son medio segundo en NTSC y 0,6 en PAL. Se deja en frames y no en
// milisegundos a proposito: el pulso tiene que caer en frame entero o el
// acorde y la melodia derivarian uno respecto al otro.
#define SLOP_BEAT_FRAMES   30
#define SLOP_BEATS_PER_BAR 16

// Duraciones de nota en pulsos, sorteadas de esta bolsa. Media 3,6 pulsos =
// 1,8 s, o sea cuatro o cinco notas por compas: una linea que se sigue, no un
// arpegio. El 2 y el 3 son los que rompen la simetria; sin ellos todas las
// frases caen en los mismos sitios.
static const u8 slopMelLen[8] = { 4, 4, 2, 6, 4, 3, 4, 2 };

// Lo que se le suma a la atenuacion de la melodia al atacar cada nota, en
// pasos de 2 dB. ESTO es lo que hace que se oiga como notas y no como un
// theremin: la envolvente del chip esta puesta para sostener, asi que sin un
// bache de volumen en cada ataque la linea es un glissando continuo sin
// principio ni fin. Baja de
// golpe y sube en rampa, que es un soplo y no un chasquido.
#define SLOP_MEL_BREATH 10 // 7,5 dB

// La rampa de la melodia es mas rapida que la del pad: 4 frames por paso son
// 0,27 s para recuperar el soplo entero. Con los 10 del pad la nota tardaria
// mas en abrirse que en acabarse.
#define SLOP_MEL_RAMP_FRAMES 3

// Una de cada cuatro notas entra deslizandose en vez de atacando, y este es
// el largo del deslizamiento. Es el portamento del cantante: con todas las
// notas atacadas la linea suena secuenciada.
#define SLOP_MEL_SLIDE_FRAMES 12

// Las tres voces desde la primera habitacion: con una progresion por medio, un
// acorde a medias no es un acorde. Lo que sigue entrando poco a poco es el
// VOLUMEN, que es de lo que iba la peticion del crescendo. Las capas que se
// superponen por seleccion son las del MENU, y el menu es la sala de maquinas.
#define MEL_VOICES_AT_START MEL_VOICES

// Y los de la sala de maquinas.

// LOS BUCLES, y aqui esta casi todo el diseno. Cada capa repite con su propio
// periodo y los cuatro periodos son primos entre si, asi que el combinado no
// vuelve a la misma posicion en 47*157*541*379 frames, que son 7000 horas. Es
// el truco con el que estaban hechas estas piezas: varios bucles de cinta de
// longitudes distintas girando a la vez. Lo que se oye como bucle es el corto,
// el golpe; los otros tres lo recolocan sin repetirse nunca.
#define MAQ_GOLPE_FRAMES  47   // 0,78 s  el ciclo de la maquina
#define MAQ_CLANC_FRAMES 157   // 2,6 s   el metal
#define MAQ_SWIRL_FRAMES 541   // 9,0 s   el color del siseo
#define MAQ_SWELL_FRAMES 379   // 6,3 s   el volumen del siseo

// El golpe cae de tono al pegar, como un piston: de la columna 1 de la tabla a
// la columna 0 en siete frames. Sin esa caida es un pitido corto y no un golpe.
#define MAQ_GOLPE_SLIDE 7

// TL de cada voz. En las tres primeras es el nivel sostenido; en las dos
// percusivas es el nivel del GOLPE -- su reposo es SLOP_TL_QUIET y la rampa
// las devuelve ahi, que es la envolvente de decaimiento.
//
// El tritono va flojo a proposito (14 son -10,5 dB): es el detalle que
// inquieta, no el cuerpo.
// El pico REAL de las percusivas es un paso mas (1,5 dB por debajo de lo que
// dice la tabla): la rampa corre en el mismo frame que el golpe, asi que el
// primer valor que llega al chip ya ha bajado uno.
static const u8 maqVoiceTL[SLOP_VOICES] = { 0, 2, 12, 0, 4 };

// Frames por paso de rampa. Las percusivas a 1 (decaen en 24 frames, 0,4 s) y
// el zumbido a 6 (tarda 2,3 s en abrirse, que es lo que hace que la sala
// arranque en vez de aparecer).
static const u8 maqRampFrames[SLOP_VOICES] = { 6, 6, 6, 1, 1 };

// EL SISEO, que es la otra mitad de la referencia ("swirling and humming white
// noise"). El PSG tiene una opcion que casi nadie usa: el generador de ruido
// puede tomar su reloj del divisor del canal de tono 2 en vez de uno de los
// tres fijos. Eso convierte el ruido blanco en ruido AFINABLE, y barrer ese
// divisor es literalmente el barrido de la referencia. El canal 2 se calla
// (solo se quiere su divisor, no su cuadrado) y el ruido sale por el canal 3.
//
// El visor de mapa usa el mismo canal de ruido para su estatica, pero con
// reloj fijo, asi que no se pelean por el divisor -- solo por el canal. De eso
// se ocupa Sfx_ambNoise.
#define MAQ_PSG_TONE_MIN 110   // divisor corto: siseo agudo
#define MAQ_PSG_TONE_MAX 900   // divisor largo: siseo grave y sucio
#define MAQ_PSG_ATT_LOUD  11   // -22 dB, el pico
#define MAQ_PSG_ATT_SOFT  14   // -28 dB
#define MAQ_PSG_ATT_TILT   2   // cuanto mas se atenua el siseo cuando es agudo
#define MAQ_PSG_VOICES     3   // el siseo entra con la tercera voz

// DOS voces al entrar, y no una: el zumbido son dos A#1 separadas 18 centesimas
// y lo que se oye es como BATEN entre si. Con una sola no hay motor, hay un
// pitido. Las otras tres (disonancia, golpe y clanc) entran por capas, una por
// planeta que se selecciona -- peticion del usuario: "las capas se superponen
// cuando se seleccionan los planetas".
#define MAQ_VOICES_AT_START 2


// Cada nota de cada voz como fnum del YM2612, en block 0. Lo calcula
// slopInitTones al arrancar.
static u16 slopNoteTone[SLOP_VOICES][SLOP_NOTES_MAX];
static u16 slopHomeTone[SLOP_VOICES]; // la del primer acorde de la progresion
static u8 slopDriftStep[SLOP_VOICES]; // pasos de registro = SLOP_DRIFT_CENTS


// LA PROGRESION DEL PAD, y qué puede cantar la melodia encima de cada acorde.
//
// Dos columnas de indices en melVoiceHz10 (bajo e interior) y una mascara de
// bits sobre las seis notas de la melodia: el bit i puesto significa que la
// nota i de melVoiceHz10[2] es nota DEL ACORDE. La melodia aterriza en una de
// esas en cada tiempo fuerte y puede pasar por las demas de camino, que es la
// regla de siempre: consonancia en los acentos, libertad entre ellos. Sin eso
// una linea sobre un pad suena a que van cada uno a lo suyo.
//
// Indices y mascaras en vez de frecuencias: asi es imposible pedir una nota
// que no este en la paleta de esa voz, o sea fuera de la escala.
//
// NO esta elegida a gusto. Se enumeraron los pares (bajo, interior) que caen
// en un acorde reconocible de Do menor con la melodia teniendo al menos dos
// notas donde posarse, y se busco el ciclo de ocho que maximiza raices
// distintas, movimiento real del bajo y opciones para la melodia, penalizando
// recorrido total e inversiones. Se repitio la busqueda para cuatro repartos
// de registro distintos al bajar las notas, y este gano en los tres criterios
// a la vez: 7 raices, 26 semitonos de recorrido (el reparto anterior gastaba
// 38 en dar la misma vuelta), 4 inversiones y 27 notas disponibles para la
// melodia en total. El bajo cambia de nota en los OCHO compases.
//
// Ninguna voz salta mas de 5 semitonos, y ese tope no es estetico: el
// deslizamiento del pad tiene un limite de centesimas por frame, asi que un
// salto de 7 tardaria 4,2 segundos y el acorde llegaria tarde a su compas.
//
// Leida de corrido es i9 - VII9 - iv7 - v - i7 - III - iio - VI, y el VI
// resuelve en el i9 del principio: por eso cicla sin costura.
#define SLOP_CHORDS 8
static const u8 slopChord[SLOP_CHORDS][MEL_PAD_VOICES] = {
    //  bajo interior      notas       grado
    { 1, 2 }, // C3  G3    i9   (Cm9)
    { 0, 1 }, // A#2 F3    VII9 (Bb9)
    { 1, 1 }, // C3  F3    iv7  (Fm7)
    { 2, 2 }, // D3  G3    v    (Gm)
    { 0, 2 }, // A#2 G3    i7   (Cm7)
    { 3, 2 }, // Eb3 G3    III  (Eb)
    { 2, 1 }, // D3  F3    iio  (Dm7b5)
    { 1, 0 }, // C3  Eb3   VI   (Ab/C)
};

// Notas del acorde dentro de la paleta de la melodia, una mascara por acorde.
//      bit 0    1   2   3    4
//          A#3  C4  D4  Eb4  F4
static const u8 slopChordTones[SLOP_CHORDS] = {
    0x0F, // i9    A#3 C4 D4 Eb4
    0x17, // VII9  A#3 C4 D4 F4
    0x1A, // iv7   C4 Eb4 F4
    0x05, // v     A#3 D4
    0x0B, // i7    A#3 C4 Eb4
    0x09, // III   A#3 Eb4
    0x16, // iio   C4 D4 F4
    0x0A, // VI    C4 Eb4
};

static u8 slopChordIdx = 0;


// El reloj. Solo corre con el acorde vivo y sin desvanecer, asi que una
// partida no arrastra la cuenta de la anterior.
static u16 beatTimer = 0;  // frames hasta el pulso siguiente; 0 = parado
// El estado propio de la melodica.
static u8 barBeat = 0;     // pulso dentro del compas, 0..SLOP_BEATS_PER_BAR-1

// La melodia. melLen son los pulsos que le quedan a la nota en curso, melIdx
// donde esta en la paleta, y melLeap guarda el signo del salto anterior
// cuando ha sido grande: la nota siguiente tiene que resolver en contra.
static u8 melLen = 0;
static u8 melIdx = 0;
static s8 melLeap = 0;

// El primer compas de la partida suena el acorde de casa, donde las voces
// acaban de nacer: sin esto el pad se moveria en su primer pulso de vida.
static bool padHome = TRUE;
// Y el de la sala de maquinas: un contador por bucle.
// Los contadores de los cuatro bucles. Cada uno cuenta hacia abajo y al llegar
// a cero dispara su capa y se rearma con su periodo.
static u16 maqGolpe = 0;
static u16 maqClanc = 0;
static u16 maqSwirl = 0;
static u16 maqSwell = 0;
static bool maqPsgOwn = TRUE;   // FALSE mientras el visor de mapa usa el ruido
static u16 maqPsgTone = 0;      // lo ultimo escrito, para no repetir
static u8 maqPsgAtt = 0xFF;

// Estado del deslizamiento de una voz hacia su nota nueva.

static u8 voiceLiveCount = 0; // las voces entran de una en una, ver Sfx_slop
static u8 voiceAtt[SLOP_VOICES];     // atenuacion objetivo, sin crescendo
static u8 voiceAttNow[SLOP_VOICES];  // la que esta puesta, para la rampa
static u8 voiceAttWritten[SLOP_VOICES];
static u16 voiceToneWritten[SLOP_VOICES];
static u8 voiceRampTick[SLOP_VOICES];
static u8 voiceDriftRate[SLOP_VOICES];
static u8 voiceDriftPhase[SLOP_VOICES];

static u16 voiceTone[SLOP_VOICES]; // registro donde la voz va a asentarse
// Donde esta AHORA mismo, que no es lo mismo: si entra un cruce mientras la
// voz todavia se desliza, el deslizamiento nuevo tiene que salir de aqui.
// Usar voiceTone daba saltos de hasta 15 semitonos en un frame, porque
// voiceTone ya valia el destino del deslizamiento en vuelo.
static u16 voiceCur[SLOP_VOICES];
static u16 gFrom[SLOP_VOICES];     // de donde viene el deslizamiento
static u16 gTimer[SLOP_VOICES];    // frames que quedan; 0 = quieta
static u16 gTotal[SLOP_VOICES];

static u8 fadeNow = SLOP_FADE_START;
static u8 fadeTarget = SLOP_FADE_START;
static u8 fadeTick = 0;
static bool slopReleasing = FALSE; // Sfx_slopStop pidio desvanecer

// Lo que cambia entre modalidades, definido mas abajo en el bloque de cada
// una. El resto del motor no sabe cual esta puesta.
static void slopClockStart(void); // arma el reloj al nacer las voces
static void slopClockStop(void);  // lo para y rearma el estado de la partida
static void slopTick(void);       // un frame de reloj: mueve lo que toque
static u8 slopRampEvery(u8 v);    // frames por paso de rampa de esta voz
static u8 slopVoiceLevel(u8 v);   // TL de reposo de esta voz
static void slopPsgInit(void);    // el PSG, que solo usa la sala de maquinas
static void slopPsgTick(void);
static void slopPsgStop(void);

// NO sale de random(): ese stream es del generador de mapas/habitaciones y
// pedirle numeros desde aqui cambiaria la generacion. Ademas es determinista
// hasta la primera pulsacion de verdad, asi que los primeros gestos serian
// identicos en cada arranque. LCG propio, y se le mezcla vtimer en cada
// cruce: cuando el jugador llega a la primera habitacion ese contador lleva
// un numero de frames que depende de lo que haya tardado en pulsar.
static u32 slopState = 1;

// Devuelve la mitad ALTA del estado: en un LCG los bits bajos tienen periodos
// muy cortos (el bit 0 alterna, el 1 tiene periodo 4...) y es justo lo que se
// lleva un "% n".
static u16 slopNext(void)
{
    slopState = (slopState * 1103515245u) + 12345u;
    return (u16) (slopState >> 16);
}

static u16 slopRange(u16 lo, u16 hi)
{
    return (u16) (lo + (slopNext() % ((hi - lo) + 1)));
}

// d pasos de fnum sobre un fnum dado valen 1200*log2((f+d)/f), que para d
// pequeno es 1731*d/f centesimas (1731 = 1200/ln 2). Al reves: d =
// centesimas*f/1731. Minimo 1, porque el registro es entero.
//
// La formula es la misma que tenia para el divisor del PSG porque es relativa.
// Lo que cambia es el SENTIDO: el divisor del PSG baja cuando sube la nota y
// el fnum sube. A todo lo que usa esto le da igual -- la deriva es un
// triangulo simetrico y el deslizamiento interpola de origen a destino -- pero
// conviene saberlo antes de tocar algo aqui.
static u8 slopStepsForCents(u16 cents, u16 tone)
{
    const u16 steps = (u16) (((u32) cents * tone) / 1731u);

    return (u8) (steps ? steps : 1);
}

// Pasa las notas a fnum del YM2612. Se calcula al arrancar y no se hornea
// porque el reloj del chip no es el mismo en las dos normas: 7670453 Hz en
// NTSC y 7600489 en PAL.
//
// fnum = f * 72 * 2^20 / (reloj * 2^block), que en punto fijo es
// fnum = (f * K) >> (5 + block) con K = 72 * 2^25 / reloj. Sale K = 315 en
// NTSC y 318 en PAL. Aqui las frecuencias vienen en decimas, de ahi el /10.
//
// TODO en block 0, que es la resolucion mas fina que da el chip: el rango de
// este voicing cae en fnum 573 a 1719 y el maximo es 2047, asi que cabe entero
// sin cambiar de block. Un paso de fnum vale entre 3,0 centesimas en la nota
// mas grave y 1,0 en la mas aguda; en el PSG eran 1,8 y 8,1.
// El block (el exponente del registro de tono) por voz. El fnum son 11 bits,
// o sea 2047 como mucho, y en block 0 eso es un techo de 208 Hz: el clanc de
// 233 Hz no cabe y necesita el block 1. Las dos notas de una misma voz TIENEN
// que compartir block, porque el deslizamiento interpola el registro en bruto.
// El block del YM por voz. El fnum son 11 bits, asi que el block 0 se queda en
// 208 Hz: el clanc de 233 Hz NO cabe y necesita el 1. Las dos notas de una
// misma voz comparten block a la fuerza, porque el deslizamiento interpola el
// registro en bruto.
static const u8 slopBlock[2][SLOP_VOICES] = {
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1 },
};

static void slopInitTones(void)
{
    const u32 k = IS_PAL_SYSTEM ? 318u : 315u;
    const bool mel = (slopMode == SLOP_MODE_MELODICO);
    u8 v, i;

    for (v = 0; v < slopVoices; v++)
    {
        const u8 notes = mel ? melVoiceCount[v] : maqVoiceCount[v];

        for (i = 0; i < notes; i++)
            slopNoteTone[v][i] = (u16)
                (((u32) (mel ? melVoiceHz10[v][i] : maqVoiceHz10[v][i]) * k)
                 / (10u << (5 + slopBlock[slopMode][v])));

        if (mel)
        {
            // El pad nace en el acorde 0; la melodia, en la nota mas grave de
            // las que ese acorde le deja (el bit mas bajo de la mascara).
            if (v < MEL_PAD_VOICES)
                slopHomeTone[v] = slopNoteTone[v][slopChord[0][v]];
            else
            {
                u8 n = 0;

                while ((n < notes) && !(slopChordTones[0] & (1 << n)))
                    n++;
                slopHomeTone[v] = slopNoteTone[v][n];
            }
        }
        else
            // En la sala no hay acorde: la nota de reposo es la columna 0.
            slopHomeTone[v] = slopNoteTone[v][0];

        // La deriva se mide sobre la nota de casa. El registro de una voz
        // cambia como mucho en un factor de 1,5 a lo largo de su lista, asi
        // que la profundidad real varia algo entre acordes -- da igual para
        // 8 centesimas.
        slopDriftStep[v] = slopStepsForCents(SLOP_DRIFT_CENTS, slopHomeTone[v]);
    }
}

// EL TIMBRE. Dos operadores: algoritmo 4 son dos parejas en paralelo
// (S1->S2 y S3->S4) y la segunda se deja muda, asi que queda S1 modulando a
// S2 y S2 como unica portadora. Eso es lo que permite que el volumen sea UNA
// escritura: el TL de la portadora. Con algoritmo 7 (los cuatro en paralelo)
// harian falta tres por voz y por frame, y el bus del Z80 no esta para eso.
//
// Portadora en MUL 1. El modulador va en MUL 2 (armonicos impares, tipo
// clarinete) salvo en el bajo, que va en MUL 1 para tener la serie completa:
// con solo impares el primer armonico seria el tercero (175 Hz) y se perderia
// el segundo (117 Hz), que es la componente mas grave que un altavoz de tele
// llega a radiar. El volumen que se PERCIBE lo manda el indice (el TL del
// modulador) mas que la portadora, porque es lo que mueve la energia del
// fundamental inaudible a los armonicos.
//
// Los desplazamientos de registro de los operadores NO son 0,1,2,3: el orden
// del chip es S1, S3, S2, S4, o sea +0, +4, +8, +12. La portadora es S2, que
// es el +8 -- de ahi el $48 del TL y no el $50.
#define SLOP_OP_S1  0
#define SLOP_OP_S3  4
#define SLOP_OP_S2  8
#define SLOP_OP_S4 12

// EL REPARTO DE CANALES. fmPart es 0 ($4000/$4001, los canales FM 1 a 3) o 1
// ($4002/$4003, los canales 4 a 6), fmCh el indice dentro de esa parte, y
// fmKey el codigo del registro $28 para pulsar la nota, que no es el mismo
// numero: ahi los canales 4, 5 y 6 se piden como 4, 5 y 6. El $28 y el $22
// son globales y viven solo en la parte 0.
//
// El contrato del latch solo afecta a la parte 0, que es la que comparte
// puerto con el DAC del driver PCM4. La parte 1 no la toca nadie mas.
// El mapa de canales es el MISMO en las dos modalidades: la melodica solo usa
// las tres primeras voces, pero deja las dos de la parte 1 bien definidas para
// que una escritura suelta nunca caiga en un canal que no es.
static const u8 fmPart[SLOP_VOICES] = { 0, 0, 0, 1, 1 };
static const u8 fmCh[SLOP_VOICES]   = { 0, 1, 2, 0, 1 };
static const u8 fmKey[SLOP_VOICES]  = { 0, 1, 2, 4, 5 };

// El TIMBRE, por modalidad. Fila 0 melodica, fila 1 sala de maquinas.
//
// Melodica: moduladores sin detune y en MUL entero, que deja todo armonico --
// un timbre de instrumento. El indice (el TL del modulador) alto: 16 son 2,0
// radianes, suficiente para llevar energia a los armonicos sin pasarse de la
// 2,405 donde J0 se anula y el fundamental desaparece.
//
// Sala de maquinas: realimentacion alta y detune en el modulador. La
// realimentacion deja de ser una sinusoide y se acerca a una sierra, y el
// detune tira las bandas laterales FUERA de la serie armonica, donde baten
// entre ellas. Eso es lo que suena a metal. Con MUL entero y DT 0 esto mismo
// sonaria a instrumento de viento, que es justo lo que no se quiere aqui.
static const u8 slopModTL[2][SLOP_VOICES] = {
    { 16, 16, 14, 16, 16 },
    { 14, 14, 18,  8, 10 },
};
static const u8 slopModMul[2][SLOP_VOICES] = {
    {  1,  2,  2,  1,  1 },
    {  1,  1,  3,  1,  7 },
};
static const u8 slopModDT[2][SLOP_VOICES] = {
    {  0,  0,  0,  0,  0 },
    {  0,  1,  3,  0,  7 },
};
static const u8 slopFeedback[2][SLOP_VOICES] = {
    {  2,  0,  0,  0,  0 },
    {  4,  4,  5,  6,  7 },
};
// Profundidad de tono del LFO. En la sala, solo dos voces lo llevan: asi esas
// dos tiemblan contra las que estan quietas, que es flameo de motor con carga.
static const u8 slopPms[2][SLOP_VOICES] = {
    {  0,  0,  0,  0,  0 },
    {  0,  1,  2,  0,  0 },
};
// $22: bit 3 enciende el LFO global, los tres bajos son la velocidad. La
// melodica no lo usa; la sala lo pone a velocidad 0, que son 3,98 Hz.
static const u8 slopLfoReg[2] = { 0x00, 0x08 };

// Lo que se le escribe al chip en un frame, montado antes de pedir el bus.
// Pedir el bus, escribir y soltarlo es una sola rafaga al final de Sfx_update:
// el Z80 tiene 223 ciclos por muestra y cada ida y vuelta le cuesta muestras.
static u16 wrTone[SLOP_VOICES];
static u8 wrTL[SLOP_VOICES];
static u8 wrToneMask = 0; // bit v: hay que escribir el tono de la voz v
static u8 wrTLMask = 0;

// Deja el latch de direccion de la parte 0 en $2A antes de soltar el bus. NO
// es opcional: el bucle del driver PCM4 escribe las muestras como datos sin
// direccion sobre $4001 dando por hecho que el latch sigue ahi. Ver la
// cabecera de la seccion.
static void slopFlush(void)
{
    u8 v;
    bool touched0 = FALSE;

    if (!(wrToneMask | wrTLMask))
        return;

    Z80_requestBus(TRUE);

    for (v = 0; v < SLOP_VOICES; v++)
    {
        const u8 part = fmPart[v];
        const u8 c = fmCh[v];

        if (!((wrToneMask | wrTLMask) & (1 << v)))
            continue;
        if (part == 0)
            touched0 = TRUE;

        if (wrToneMask & (1 << v))
        {
            // El $A4 primero: ahi va el block y los 3 bits altos del fnum, y
            // el chip no aplica nada hasta que llega el $A0 con los 8 bajos.
            YM2612_writeReg(part, (u8) (0xA4 + c),
                            (u8) ((slopBlock[slopMode][v] << 3) | (wrTone[v] >> 8)));
            YM2612_writeReg(part, (u8) (0xA0 + c), (u8) (wrTone[v] & 0xFF));
        }
        if (wrTLMask & (1 << v))
            YM2612_writeReg(part, (u8) (0x40 + SLOP_OP_S2 + c), wrTL[v]);
    }

    // Si la rafaga solo ha tocado la parte 1 el latch de la parte 0 sigue
    // donde lo dejo el driver, y entonces esta escritura no hace falta.
    if (touched0)
        YM2612_write(0, 0x2A); // el latch, de vuelta donde el driver lo espera
    Z80_releaseBus();

    wrToneMask = 0;
    wrTLMask = 0;
}

// Monta los tres canales FM y los deja mudos y pulsados. Se pulsa UNA vez y no
// se vuelve a soltar: con AR al maximo y D1L/D1R a cero la nota se sostiene
// para siempre, y todo el volumen lo lleva el TL. Asi no hay clics de key on.
static void slopInitFM(void)
{
    u8 v;

    // La rafaga se parte en trozos de siete escrituras, una toma de bus por
    // operador. De un tiron son 157, y el bucle del driver PCM4 tiene que
    // escribir una muestra cada 223 ciclos del Z80: 157 seguidas le quitan el
    // bus unos diez samples y eso se oye. Antes pasaba solo al arrancar, con
    // el driver todavia sin nada que sonar; ahora pasa en cada cambio de
    // escena, con el bombo pudiendo estar a medias.
    Z80_requestBus(TRUE);
    YM2612_writeReg(0, 0x22, slopLfoReg[slopMode]); // el LFO del chip (global)
    YM2612_write(0, 0x2A);
    Z80_releaseBus();

    for (v = 0; v < SLOP_VOICES; v++)
    {
        static const u8 ops[4] = { SLOP_OP_S1, SLOP_OP_S3, SLOP_OP_S2, SLOP_OP_S4 };
        const u8 part = fmPart[v];
        const u8 c = fmCh[v];
        u8 o;

        for (o = 0; o < 4; o++)
        {
            const u8 op = ops[o];

            Z80_requestBus(TRUE);
            // El modulador con su MUL y su DT; la portadora en MUL 1 y sin DT.
            YM2612_writeReg(part, (u8) (0x30 + op + c),
                            (op == SLOP_OP_S1)
                              ? (u8) ((slopModDT[slopMode][v] << 4) | slopModMul[slopMode][v])
                              : 0x01);
            // AR al maximo (31), KS 0: la nota ya esta arriba al pulsarla.
            YM2612_writeReg(part, (u8) (0x50 + op + c), 0x1F);
            // D1R y D2R a 0: no decae, se sostiene.
            YM2612_writeReg(part, (u8) (0x60 + op + c), 0x00);
            YM2612_writeReg(part, (u8) (0x70 + op + c), 0x00);
            // D1L 0 (sostiene al maximo) y RR 15.
            YM2612_writeReg(part, (u8) (0x80 + op + c), 0x0F);
            YM2612_writeReg(part, (u8) (0x90 + op + c), 0x00); // SSG-EG fuera
            // TL: el modulador con su indice, la portadora muda de momento, y
            // la pareja S3/S4 muda del todo -- es la que no se usa.
            {
                u8 tl = SLOP_TL_SILENT;

                if (op == SLOP_OP_S1)
                    tl = slopModTL[slopMode][v];
                YM2612_writeReg(part, (u8) (0x40 + op + c), tl);
            }

            // El latch de la parte 0 vuelve a su sitio ANTES de soltar, igual
            // que en slopFlush: el contrato del driver es el mismo aqui.
            if (part == 0)
                YM2612_write(0, 0x2A);
            Z80_releaseBus();
        }

        Z80_requestBus(TRUE);
        YM2612_writeReg(part, (u8) (0xB0 + c), // realimentacion y algoritmo 4
                        (u8) ((slopFeedback[slopMode][v] << 3) | 0x04));
        // Los dos canales de salida, y la profundidad de tono del LFO.
        YM2612_writeReg(part, (u8) (0xB4 + c), (u8) (0xC0 | slopPms[slopMode][v]));
        YM2612_writeReg(0, 0x28, (u8) (0xF0 | fmKey[v])); // key on, 4 slots
        YM2612_write(0, 0x2A); // la key on es de la parte 0 siempre
        Z80_releaseBus();
    }
}

// Triangulo de -64 a +63 sobre una fase de 8 bits.
static s16 slopLfo(u8 phase)
{
    const s16 up = (phase < 128) ? (s16) phase : (s16) (255 - phase);

    return (s16) (up - 64);
}

// La S, interpolada entre los 33 puntos de la tabla. progress va de 0 a 1023,
// o sea 5 bits de fraccion entre punto y punto. Con 3 bits la interpolacion
// era el cuello de botella de la suavidad: dos puntos contiguos se diferencian
// hasta en 29 de 255, asi que en octavos quedaban escalones por encima del
// salto real de la curva.
static u16 slopSig(u16 progress)
{
    const u16 i = (u16) (progress >> 5);
    const u16 frac = (u16) (progress & 31);
    const u16 a = slopSigmoid[i];
    const u16 b = slopSigmoid[i + 1];

    return (u16) (a + (((b - a) * frac) >> 5));
}

// --- Vida del acorde --------------------------------------------------------

// Lo llama Sfx_loadDriver, y el orden SI importa: slopInitFM escribe
// registros del YM2612, y Sfx_loadDriver carga antes el driver PCM4, que pasa
// por YM2612_reset (deja todos los TL en 127) y luego por el initDAC del
// propio driver. Montar el timbre antes de eso seria tirarlo a la basura.
static void slopInit(void)
{
    u8 v;

    slopInitTones();
    slopInitFM();
    slopPsgInit();
    Sfx_slopStop();
    // slopStop pide desvanecer; al arrancar se quiere silencio ya.
    for (v = 0; v < SLOP_VOICES; v++)
    {
        voiceAttNow[v] = SLOP_TL_SILENT;
        voiceAttWritten[v] = SLOP_TL_SILENT;
    }
    slopReleasing = FALSE;
}

void Sfx_slopStop(void)
{
    u8 v;

    // No corta: pide desvanecer, y Sfx_update lleva la rampa. Un acorde
    // sostenido cortado en seco se oye como un chasquido.
    slopReleasing = TRUE;
    for (v = 0; v < SLOP_VOICES; v++)
    {
        gTimer[v] = 0;
        // El destino de la rampa es QUIET; el 127 se escribe de golpe al
        // llegar, que es inaudible desde -36 dB y ahorra 40 pasos de rampa.
        voiceAtt[v] = SLOP_TL_QUIET;
    }
    slopClockStop();
    // La proxima partida vuelve a entrar desde abajo y de una voz en una.
    fadeNow = SLOP_FADE_START;
    fadeTarget = SLOP_FADE_START;
    fadeTick = 0;
}

// --- EL MOTOR MELODICO -----------------------------------------------------

// Mueve el PAD al acorde siguiente de la progresion. La melodia no entra
// aqui: tiene su propio ritmo y la mueve slopMelodyNote. Cada voz se desliza
// desde donde este, no desde su nota de casa.
static void slopAdvanceChord(void)
{
    u8 v;

    slopChordIdx = (u8) ((slopChordIdx + 1) % SLOP_CHORDS);

    for (v = 0; v < MEL_PAD_VOICES; v++)
    {
        const u16 target = slopNoteTone[v][slopChord[slopChordIdx][v]];
        const u16 from = voiceCur[v];
        u16 glide = slopRange(SLOP_GLIDE_MIN, SLOP_GLIDE_MAX);

        if (target == from) // ya esta ahi, nada que deslizar
        {
            voiceTone[v] = target;
            gTimer[v] = 0;
            continue;
        }

        // El deslizamiento dura al menos lo que pide el tope de centesimas
        // por frame, asi que un salto grande tarda mas que uno pequeno, que
        // ademas es como se toca de verdad. El salto se mide sobre el propio
        // registro (1731*d/reg centesimas, con 1731 = 1200/ln 2) en vez de
        // sobre una tabla de semitonos, asi que vale para cualquier nota de
        // la lista. La pendiente maxima de la sigmoide es k/4 = 3,6 veces la
        // media, de ahi el 36.
        {
            const u16 d = (u16) ((target > from) ? (target - from) : (from - target));
            const u16 cents = (u16) (((u32) d * 1731u) / from);
            const u16 least = (u16) ((cents * 36u) / (SLOP_BEND_CENTS_PER_FRAME * 10u));

            if (glide < least)
                glide = least;
        }

        gFrom[v] = from;
        voiceTone[v] = target;
        gTotal[v] = glide;
        gTimer[v] = glide;
    }
}

// Elige la nota siguiente de la melodia, devuelta como indice en la paleta.
// strong = cae en el primer pulso del compas.
//
// Las reglas son las de cualquier linea cantable y estan en este orden de
// importancia: por grados casi siempre, los saltos solo a notas del acorde,
// un salto se resuelve en contra y por grados, en los tiempos fuertes solo
// notas del acorde, y nunca dos veces la misma nota seguida. Nada de esto es
// mio, es contrapunto de primer curso; lo que hace es que una secuencia de
// notas suene a frase y no a sorteo.
static u8 slopMelodyPick(bool strong)
{
    const u8 n = melVoiceCount[MEL_MELODY_VOICE];
    const u8 tones = slopChordTones[slopChordIdx];
    const u8 mid = (u8) (n / 2);
    u8 w[SLOP_NOTES_MAX];
    u16 total = 0;
    u8 i;

    for (i = 0; i < n; i++)
    {
        const s8 d = (s8) ((s8) i - (s8) melIdx);
        const u8 ad = (u8) ((d < 0) ? -d : d);
        const bool chordTone = (tones & (1 << i)) != 0;
        u8 weight;

        if (ad == 0)                          // nunca repite nota
            weight = 0;
        else if (strong)                      // tiempo fuerte: solo el acorde
            weight = chordTone ? (u8) ((ad <= 4) ? (8 - ad) : 1) : 0;
        else if (ad == 1)                     // grado conjunto, la norma
            weight = chordTone ? 20 : 10;
        else if (ad == 2)
            weight = chordTone ? 6 : 2;
        else if (ad <= 4)                     // salto, y solo al acorde
            weight = chordTone ? 3 : 0;
        else
            weight = 0;

        // Resolucion del salto anterior: en contra y por grados.
        if ((melLeap != 0) &&
            (((melLeap > 0) == (d > 0)) || (ad > 2)))
            weight = 0;

        // Sesgo de contorno hacia el centro de la paleta. Sin esto la linea se
        // queda rondando un extremo, y arriba es donde mas chilla.
        if (((melIdx > mid) && (d < 0)) || ((melIdx < mid) && (d > 0)))
            weight = (u8) (weight + (weight >> 1));

        w[i] = weight;
        total = (u16) (total + weight);
    }

    // Sin salida legal -- pasa cuando el acorde nuevo solo tiene notas hacia
    // donde la resolucion del salto no deja ir. Entonces manda la consonancia:
    // la nota del acorde mas cercana, prefiriendo la direccion contraria al
    // salto si por ahi hay alguna.
    if (total == 0)
    {
        u8 pass, best = melIdx, bestD = 255;

        for (pass = 0; (pass < 2) && (bestD == 255); pass++)
            for (i = 0; i < n; i++)
            {
                const s8 d = (s8) ((s8) i - (s8) melIdx);
                const u8 ad = (u8) ((d < 0) ? -d : d);

                if (!ad || !(tones & (1 << i)))
                    continue;
                // Primera pasada: solo en contra del salto anterior.
                if ((pass == 0) && (melLeap != 0) && ((melLeap > 0) == (d > 0)))
                    continue;
                if (ad < bestD)
                {
                    bestD = ad;
                    best = i;
                }
            }
        return best;
    }

    {
        u16 r = (u16) (slopNext() % total);

        for (i = 0; i < n; i++)
        {
            if (r < w[i])
                return i;
            r = (u16) (r - w[i]);
        }
    }
    return melIdx; // inalcanzable: los pesos suman total
}

// Ataca la nota siguiente de la melodia y le pone duracion.
static void slopMelodyNote(bool strong)
{
    const u8 v = MEL_MELODY_VOICE;
    const u8 prev = melIdx;
    s8 d;
    u8 len;

    melIdx = slopMelodyPick(strong);
    d = (s8) ((s8) melIdx - (s8) prev);
    melLeap = (s8) ((((d < 0) ? -d : d) >= 3) ? ((d > 0) ? 1 : -1) : 0);

    // Duracion, recortada para que la frase acabe justo en la barra: asi el
    // primer pulso del compas siempre trae nota nueva Y acorde nuevo a la vez.
    len = slopMelLen[slopNext() & 7];
    {
        const u8 left = (u8) (SLOP_BEATS_PER_BAR - barBeat);

        if (len > left)
            len = left;
    }
    melLen = len;

    // El tono. Una de cada cuatro entra deslizandose y el resto de golpe: un
    // salto de tono con el volumen en el suelo del soplo no se oye como un
    // chasquido, se oye como el principio de una nota, que es justo lo que le
    // falta a esta voz para ser una melodia.
    gFrom[v] = voiceCur[v];
    voiceTone[v] = slopNoteTone[v][melIdx];
    if ((slopNext() & 3) == 0)
    {
        gTotal[v] = SLOP_MEL_SLIDE_FRAMES;
        gTimer[v] = SLOP_MEL_SLIDE_FRAMES;
    }
    else
    {
        gTimer[v] = 0;
        voiceCur[v] = voiceTone[v];
    }

    // El soplo: baja de golpe y la rampa de Sfx_update lo sube.
    voiceAttNow[v] = (u8) (((voiceAttNow[v] + SLOP_MEL_BREATH) > SLOP_TL_QUIET)
                           ? SLOP_TL_QUIET : (voiceAttNow[v] + SLOP_MEL_BREATH));
    voiceRampTick[v] = 0;
}

// La melodia va 3 a 3,75 dB por encima del pad, que es lo que hace que se oiga
// como la voz principal y no como una tercera nota del acorde. El reparto
// desigual del pad es a proposito: una triada perfectamente equilibrada suena
// a organo. La melodia esta en el techo del registro, asi que esa diferencia
// se mantiene bajando el pad, no subiendo la melodia.
static u8 melVoiceLevel(u8 v)
{
    if (v == MEL_MELODY_VOICE)
        return SLOP_TL_MELODY;
    return (u8) (SLOP_TL_FLOOR + (slopNext() % SLOP_TL_JITTER));
}

static u8 melRampEvery(u8 v)
{
    return (v == MEL_MELODY_VOICE) ? SLOP_MEL_RAMP_FRAMES : SLOP_ATTACK_FRAMES;
}

static void melClockStart(void)
{
    beatTimer = SLOP_BEAT_FRAMES;
    barBeat = 0;
    melLen = 0;
    melLeap = 0;
    padHome = TRUE;
}

static void melClockStop(void)
{
    slopChordIdx = 0; // la proxima partida vuelve a empezar por la casa
    beatTimer = 0;
    barBeat = 0;
    melLen = 0;
    melLeap = 0;
    padHome = TRUE;
}

// El pulso: el acorde cae en el primer tiempo del compas y la melodia coloca
// sus notas en la rejilla de dentro.
static void melTick(void)
{
    if (beatTimer > 0)
        beatTimer--;
    if (beatTimer == 0)
    {
        const bool downbeat = (barBeat == 0);

        beatTimer = SLOP_BEAT_FRAMES;

        // El pad primero: la melodia lee la mascara del acorde nuevo.
        if (downbeat)
        {
            if (padHome)
                padHome = FALSE;
            else
                slopAdvanceChord();
        }

        if (melLen > 0)
            melLen--;
        if (melLen == 0)
            slopMelodyNote(downbeat);

        barBeat = (u8) ((barBeat + 1) % SLOP_BEATS_PER_BAR);
    }
}

// La melodica no toca el PSG: el siseo es solo de la sala de maquinas, y el
// canal de ruido se lo queda entero el visor de mapa.

// --- LA SALA DE MAQUINAS ---------------------------------------------------

static u8 maqVoiceLevel(u8 v)
{
    // Las dos percusivas DESCANSAN en QUIET: su nivel de golpe lo pone
    // slopTick y la rampa las devuelve aqui, y esa vuelta es su envolvente de
    // decaimiento. Las tres del zumbido descansan en su nivel y ya esta.
    return (v >= SLOP_GOLPE_VOICE) ? SLOP_TL_QUIET : maqVoiceTL[v];
}

static u8 maqRampEvery(u8 v)
{
    return maqRampFrames[v];
}

static void maqClockStart(void)
{
    // Cada partida arranca los cuatro bucles en una fase distinta, asi que dos
    // partidas no oyen la maquina en el mismo sitio de su ciclo.
    beatTimer = 1;
    maqGolpe = (u16) (slopNext() % MAQ_GOLPE_FRAMES);
    maqClanc = (u16) (slopNext() % MAQ_CLANC_FRAMES);
    maqSwirl = (u16) (slopNext() % MAQ_SWIRL_FRAMES);
    maqSwell = (u16) (slopNext() % MAQ_SWELL_FRAMES);
}

static void maqClockStop(void)
{
    beatTimer = 0;
    maqGolpe = 0;
    maqClanc = 0;
    maqSwirl = 0;
    maqSwell = 0;
}

// Triangulo de 0 a 255 sobre un periodo cualquiera. Hace falta uno aparte del
// slopLfo de la deriva porque ese va sobre una fase de 8 bits y estos bucles
// tienen periodos primos, que no son potencias de dos.
static u8 maqTri(u16 t, u16 period)
{
    const u16 half = (u16) (period >> 1);
    const u16 x = (t < half) ? t : (u16) (period - 1u - t);

    return (u8) (((u32) x * 255u) / half);
}

static void maqTick(void)
{
    // EL GOLPE, el bucle corto y lo unico que se oye como ciclo. Cae de tono
    // (de la columna 1 de la tabla a la columna 0) y de volumen a la vez: sin
    // la caida de tono es un pitido corto, no un piston.
    if (voiceLiveCount > SLOP_GOLPE_VOICE)
    {
        if (maqGolpe == 0)
        {
            const u8 v = SLOP_GOLPE_VOICE;

            maqGolpe = MAQ_GOLPE_FRAMES;
            gFrom[v] = slopNoteTone[v][1];
            voiceCur[v] = slopNoteTone[v][1];
            voiceTone[v] = slopNoteTone[v][0];
            gTotal[v] = MAQ_GOLPE_SLIDE;
            gTimer[v] = MAQ_GOLPE_SLIDE;
            voiceAttNow[v] = maqVoiceTL[v]; // arriba de golpe, y la rampa baja
            voiceRampTick[v] = 0;
        }
        maqGolpe--;
    }

    // EL CLANC, el bucle de 2,6 s. Sin caida de tono: el metal no cae, suena y
    // se apaga.
    if (voiceLiveCount > SLOP_CLANC_VOICE)
    {
        if (maqClanc == 0)
        {
            const u8 v = SLOP_CLANC_VOICE;

            maqClanc = MAQ_CLANC_FRAMES;
            voiceAttNow[v] = maqVoiceTL[v];
            voiceRampTick[v] = 0;
        }
        maqClanc--;
    }
}

static void maqPsgInit(void)
{
    // El canal 2 CALLADO: de el solo se quiere el divisor, que es lo que le
    // sirve de reloj al generador de ruido. Su cuadrado no se oye nunca.
    PSG_setEnvelope(2, PSG_ENVELOPE_MIN);
    PSG_setNoise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_TONE3);
    PSG_setEnvelope(3, PSG_ENVELOPE_MIN);
    maqPsgTone = 0;
    maqPsgAtt = 0xFF;
}

static void maqPsgTick(void)
{
    u16 tone;
    u8 att;
    u8 swirl;
    u8 swell;

    if (!maqPsgOwn)
        return; // el visor de mapa esta usando el canal de ruido

    if (slopReleasing)
    {
        // Un siseo cortado en seco se oye. Se sube de uno en uno hasta callar.
        if (maqPsgAtt < PSG_ENVELOPE_MIN)
        {
            maqPsgAtt++;
            PSG_setEnvelope(3, maqPsgAtt);
        }
        return;
    }

    if (voiceLiveCount < MAQ_PSG_VOICES)
        return;

    if (maqSwirl == 0)
        maqSwirl = MAQ_SWIRL_FRAMES;
    maqSwirl--;
    if (maqSwell == 0)
        maqSwell = MAQ_SWELL_FRAMES;
    maqSwell--;

    // Dos triangulos con periodos primos entre si: uno sobre el divisor (el
    // COLOR del siseo) y otro sobre el volumen. Que no coincidan nunca es lo
    // que hace que el siseo gire en vez de latir.
    swirl = maqTri(maqSwirl, MAQ_SWIRL_FRAMES);
    swell = maqTri(maqSwell, MAQ_SWELL_FRAMES);

    tone = (u16) (MAQ_PSG_TONE_MIN
                  + (((u32) (MAQ_PSG_TONE_MAX - MAQ_PSG_TONE_MIN)
                      * swirl) >> 8));
    // Entre 255 y 256 hay un paso entero de atenuacion (2 dB), asi que aqui se
    // divide de verdad: con >>8 el extremo fuerte se quedaba sin llegar.
    att = (u8) (MAQ_PSG_ATT_SOFT
                - (((u16) (MAQ_PSG_ATT_SOFT - MAQ_PSG_ATT_LOUD)
                    * swell) / 255u));

    // El siseo agudo se oye mucho mas que el grave al mismo nivel electrico, y
    // el momento que molesta es agudo y fuerte a la vez. Se atenua por color, y
    // en la esquina agudo-flojo el tope de la escala lo deja callado un rato.
    att = (u8) (att + ((MAQ_PSG_ATT_TILT * (255u - swirl)) / 255u));
    if (att > PSG_ENVELOPE_MIN)
        att = PSG_ENVELOPE_MIN;

    if (tone != maqPsgTone)
    {
        maqPsgTone = tone;
        PSG_setTone(2, tone);
    }
    if (att != maqPsgAtt)
    {
        maqPsgAtt = att;
        PSG_setEnvelope(3, att);
    }
}

static void maqPsgStop(void)
{
    if (!maqPsgOwn)
        return;
    PSG_setEnvelope(3, PSG_ENVELOPE_MIN);
    maqPsgAtt = PSG_ENVELOPE_MIN;
}


// --- El reparto entre las dos modalidades -----------------------------------
//
// Las dos conviven en ejecucion: el menu suena melodico y la partida suena a
// sala de maquinas. Lo unico que comparten es el motor (bus, rampas, deriva,
// deslizamiento, crescendo); de aqui para arriba cada una es la suya.

static void slopTick(void)
{
    if (slopMode == SLOP_MODE_MELODICO) melTick(); else maqTick();
}

static u8 slopRampEvery(u8 v)
{
    return (slopMode == SLOP_MODE_MELODICO) ? melRampEvery(v) : maqRampEvery(v);
}

static u8 slopVoiceLevel(u8 v)
{
    return (slopMode == SLOP_MODE_MELODICO) ? melVoiceLevel(v) : maqVoiceLevel(v);
}

static void slopClockStart(void)
{
    if (slopMode == SLOP_MODE_MELODICO) melClockStart(); else maqClockStart();
}

static void slopClockStop(void)
{
    if (slopMode == SLOP_MODE_MELODICO) melClockStop(); else maqClockStop();
}

// El PSG es solo de la sala de maquinas. Callarlo, en cambio, se hace siempre:
// al salir de la sala hay que apagar el siseo aunque ya no sea la modalidad.
static void slopPsgInit(void)
{
    if (slopMode == SLOP_MODE_MAQUINAS) maqPsgInit();
}

static void slopPsgTick(void)
{
    if (slopMode == SLOP_MODE_MAQUINAS) maqPsgTick();
}

static void slopPsgStop(void)
{
    maqPsgStop();
}

void Sfx_ambNoise(bool own)
{
    maqPsgOwn = own;
    if (own && (slopMode == SLOP_MODE_MAQUINAS))
    {
        // El visor de mapa ha dejado el ruido en su reloj fijo: hay que volver
        // a engancharlo al divisor del canal 2 y forzar la reescritura.
        PSG_setNoise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_TONE3);
        maqPsgTone = 0;
        maqPsgAtt = 0xFF;
    }
}

// Calla lo que suene, rehace timbre y tonos, y deja el ambiente listo para
// entrar otra vez por capas. Pedir la modalidad que ya esta puesta no hace
// nada, asi que se puede llamar cada frame sin reiniciar el ambiente.
static void slopSetMode(u8 mode)
{
    u8 v;

    if (mode == slopMode)
        return;

    maqPsgStop();
    // El visor de mapa solo existe dentro de la partida, asi que un cambio de
    // escena significa que ya no esta abierto: el canal de ruido vuelve a ser
    // nuestro. Sin esto, salirse al menu CON el visor abierto (el combo de
    // reset dispara desde dentro) dejaba el siseo del menu mudo para siempre.
    maqPsgOwn = TRUE;

    // Silencio INMEDIATO, no desvanecido: lo que viene detras reescribe el
    // timbre de los mismos canales, y una voz sonando mientras le cambian el
    // modulador debajo es un chasquido.
    for (v = 0; v < SLOP_VOICES; v++)
    {
        voiceAttNow[v] = SLOP_TL_SILENT;
        voiceAttWritten[v] = SLOP_TL_SILENT;
        voiceToneWritten[v] = 0;
        wrTL[v] = SLOP_TL_SILENT;
        wrTLMask |= (u8) (1 << v);
    }
    slopFlush();

    slopClockStop(); // el reloj de la que se va
    slopMode = mode;
    slopVoices = (mode == SLOP_MODE_MELODICO) ? MEL_VOICES : MAQ_VOICES;
    voiceLiveCount = 0;
    slopReleasing = FALSE;
    fadeNow = SLOP_FADE_START;
    fadeTarget = SLOP_FADE_START;
    fadeTick = 0;

    slopInitTones();
    slopInitFM();
    slopPsgInit();
}

void Sfx_ambScene(bool menu)
{
    slopSetMode(menu ? SLOP_MODE_MAQUINAS : SLOP_MODE_MELODICO);
}

// Arranca las voces que toquen y avanza el acorde.
void Sfx_slop(void)
{
    bool started = FALSE;

    if (!enabled)
        return;

    slopReleasing = FALSE;
    slopState += (u16) vtimer;

    {
        // La primera habitacion arranca con mas de una voz; despues entra una
        // mas por cruce hasta el total.
        const u8 atStart = (slopMode == SLOP_MODE_MELODICO) ? MEL_VOICES_AT_START
                                                            : MAQ_VOICES_AT_START;
        u8 want = (voiceLiveCount == 0) ? atStart : (u8) (voiceLiveCount + 1);

        if (want > slopVoices)
            want = slopVoices;

        while (voiceLiveCount < want)
        {
            const u8 nv = voiceLiveCount;

            voiceAtt[nv] = slopVoiceLevel(nv);
            voiceAttNow[nv] = SLOP_TL_QUIET; // entra subiendo, ver Sfx_update
            voiceRampTick[nv] = 0;
            voiceDriftRate[nv] = (u8) slopRange(SLOP_DRIFT_RATE_MIN, SLOP_DRIFT_RATE_MAX);
            voiceDriftPhase[nv] = (u8) slopNext();
            voiceTone[nv] = slopHomeTone[nv];
            voiceCur[nv] = slopHomeTone[nv];
            gTimer[nv] = 0;
            voiceLiveCount++;
            started = TRUE;
        }
    }

    // Si venia de un desvanecido a medias, las voces vivas vuelven a su nivel.
    {
        u8 v;

        for (v = 0; v < voiceLiveCount; v++)
            if (voiceAtt[v] == SLOP_TL_QUIET)
                voiceAtt[v] = slopVoiceLevel(v);
    }

    // El acorde NO se toca aqui: lo mueve el temporizador de Sfx_update. Lo
    // unico que hace el cruce es arrancar la cuenta cuando nacen las voces,
    // para que el acorde de casa suene entero antes del primer cambio.
    // beatTimer solo vale 0 con el reloj parado: el bloque de Sfx_update lo
    // rearma en el mismo frame en que llega a cero. Asi una partida que
    // empieza mientras la anterior se desvanece no salta de acorde de golpe.
    if (started || (beatTimer == 0))
        slopClockStart();

    if (fadeTarget > 0) // una habitacion mas, menos atenuacion extra
        fadeTarget = (fadeTarget > SLOP_FADE_PER_ROOM) ? (u8) (fadeTarget - SLOP_FADE_PER_ROOM) : 0;
}

// Donde esta esta voz ahora mismo, en pasos de registro: su nota asentada, o
// un punto del deslizamiento hacia ella si todavia esta de camino.
static u16 slopVoiceTone(u8 v)
{
    const u16 to = voiceTone[v];
    u16 e;
    s16 span;

    if (gTimer[v] == 0)
    {
        voiceCur[v] = to;
        return to;
    }

    gTimer[v]--;
    e = slopSig((u16) (((u32) (gTotal[v] - gTimer[v]) * 1023u) / gTotal[v]));
    span = (s16) (to - gFrom[v]);
    voiceCur[v] = (u16) (gFrom[v] + (((s32) span * e) >> 8));

    return voiceCur[v];
}

void Sfx_update(void)
{
    u8 v;
    u8 silent = 0;

    if (voiceLiveCount == 0)
        return;

    if (!enabled)
    {
        // Apagar el sonido con la C del menu calla el acorde en el acto.
        for (v = 0; v < SLOP_VOICES; v++)
        {
            voiceAttNow[v] = SLOP_TL_SILENT;
            voiceAttWritten[v] = SLOP_TL_SILENT;
            wrTL[v] = SLOP_TL_SILENT;
            wrTLMask |= (u8) (1 << v);
        }
        slopPsgStop();
        slopFlush();
        voiceLiveCount = 0;
        return;
    }

    // EL RELOJ. Antes del bucle de voces a proposito: lo que dispare puede
    // querer salir de voiceCur, que es donde la voz quedo el frame pasado.
    if (!slopReleasing)
        slopTick();

    // El crescendo se desliza hacia su objetivo en vez de saltar.
    if (fadeNow > fadeTarget)
    {
        fadeTick++;
        if (fadeTick >= SLOP_FADE_FRAMES)
        {
            fadeTick = 0;
            fadeNow--;
        }
    }

    for (v = 0; v < voiceLiveCount; v++)
    {
        const u16 want = (u16) (voiceAtt[v] + (slopReleasing ? 0 : fadeNow));
        u16 tone;
        u8 att;

        // Rampa de volumen: SLOP_TL_STEP cada N frames, arriba o abajo. Esto
        // es la envolvente: la del chip esta puesta para sostener y no se usa.
        {
            const u8 every = slopReleasing ? SLOP_FADEOUT_FRAMES
                                           : slopRampEvery(v);
            const u8 target = (u8) ((want > SLOP_TL_QUIET) ? SLOP_TL_QUIET : want);

            if (voiceAttNow[v] != target)
            {
                voiceRampTick[v]++;
                if (voiceRampTick[v] >= every)
                {
                    u16 n = voiceAttNow[v];

                    voiceRampTick[v] = 0;
                    // Sin pasarse del objetivo: el paso es de 2 y la distancia
                    // puede ser impar, y oscilar alrededor seria un tremolo.
                    if (n > target)
                        n = (u16) ((n - target >= SLOP_TL_STEP) ? n - SLOP_TL_STEP : target);
                    else
                        n = (u16) ((target - n >= SLOP_TL_STEP) ? n + SLOP_TL_STEP : target);
                    voiceAttNow[v] = (u8) n;
                }
            }
        }
        att = voiceAttNow[v];

        // Fin del desvanecido de esta voz: del -36 dB de QUIET al 127 del
        // registro de golpe, que desde ahi no se oye.
        if (slopReleasing && (att >= SLOP_TL_QUIET))
        {
            if (voiceAttWritten[v] != SLOP_TL_SILENT)
            {
                voiceAttWritten[v] = SLOP_TL_SILENT;
                wrTL[v] = SLOP_TL_SILENT;
                wrTLMask |= (u8) (1 << v);
            }
            silent++;
            continue;
        }

        // La nota (o el deslizamiento hacia ella) con la deriva lenta encima.
        tone = slopVoiceTone(v);
        tone = (u16) (tone + ((slopLfo(voiceDriftPhase[v]) * slopDriftStep[v]) >> 6));
        voiceDriftPhase[v] = (u8) (voiceDriftPhase[v] + voiceDriftRate[v]);

        // Solo lo que ha cambiado. Importa: una voz asentada cambia de fnum
        // una vez cada quince frames o mas, asi que el bus del Z80 se toma
        // para dos o tres escrituras y no para nueve.
        if (voiceToneWritten[v] != tone)
        {
            voiceToneWritten[v] = tone;
            wrTone[v] = tone;
            wrToneMask |= (u8) (1 << v);
        }
        if (voiceAttWritten[v] != att)
        {
            voiceAttWritten[v] = att;
            wrTL[v] = att;
            wrTLMask |= (u8) (1 << v);
        }
    }

    slopPsgTick(); // no pasa por el bus del Z80: el PSG esta en el VDP
    slopFlush();

    // Desvanecido terminado: se cierra aqui y no en Sfx_slopStop, porque la
    // rampa necesita que las voces sigan vivas para poder bajarlas. Sin esto
    // la partida siguiente arrancaria con el acorde ya montado en vez de
    // construirlo voz a voz.
    if (slopReleasing && (silent >= voiceLiveCount))
        voiceLiveCount = 0;
}
