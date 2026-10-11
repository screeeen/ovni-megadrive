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
#define SFX_VOL_MIN            6
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


// --- Chordslop --------------------------------------------------------------
// Un acorde que suena siempre y no cambia de identidad: lo que varia son
// GESTOS de pitch bend que salen de la nota y vuelven a ella. El modelo es el
// Chord Slop Lab del usuario, y de ahi vienen tres decisiones que no son mias:
//
//  - VOCES FIJAS, cada una en su nota. El movimiento son bends que aterrizan
//    y regresan, no reasignaciones. La armonia nunca se deshace.
//  - BENDS RESTRINGIDOS AL ACORDE (sus "safeBends"): cada voz tiene una lista
//    de destinos en semitonos que caen en notas de la escala.
//  - LA FORMA DE LA CURVA: sus arrays de bend son planos, suben en S, se
//    mantienen largo rato y bajan en S. Esa S es una logistica de k = 14,3,
//    ajustada contra sus datos con error maximo 0,02, y esta en slopSigmoid.
//
// POR QUE PSG Y NO FM, QUE ES LA SEGUNDA VEZ QUE SE DECIDE ESTO:
// Hubo una version en FM con cinco sinusoidales. Sobre el papel era mejor
// (sinusoide pura, resolucion de tono de una centesima, cinco voces, estereo),
// pero el usuario la oyo en la maquina: "muy feo y rudo, incluso sonaba mejor
// la primera implementacion". Y tenia razon. Una sinusoide pura es LO PEOR que
// se le puede pedir a un YM2612 de Model 1, porque ese chip tiene el "ladder
// effect" -- una discontinuidad en el cruce por cero de su DAC -- y una
// sinusoide pasa mucho tiempo justo ahi, cerca de cero y con pendiente baja,
// dejando la distorsion al descubierto. La onda cuadrada del PSG es inmune:
// esta casi siempre a riel, y el PSG es un conversor mucho mas simple.
//
// O sea que "sinusoide = suave" es al reves en este hardware. Si alguien
// vuelve a querer FM aqui, el historial de git tiene esa version entera y
// funcionaba; lo que no funcionaba era como suena.
//
// Lo que cuesta el PSG: tres voces en vez de cinco, un solo canal de salida
// (no hay panoramico), 2 dB de resolucion en el volumen y un registro de tono
// que es un divisor, asi que un paso no vale lo mismo en toda la escala. Lo
// que se gana: suena bien, no hay que pedirle el bus al Z80 ni una vez, y el
// driver PCM4 de los efectos no se entera de que existe.
//
// El canal 4 del PSG, el de ruido, NO se toca: lo usa el siseo de estatica
// del visor de mapa (main.c).

#define SLOP_VOICES 3

// Las notas que puede tocar cada voz, en Hz y de grave a aguda. Antes era una
// nota base por voz mas la lista de "safeBends" del export del usuario, que
// dejaba al bajo con una sola opcion: ocho acordes sobre un pedal de C. Eso
// era el techo de la musicalidad, asi que ahora las tres voces tienen notas
// propias y el bajo se mueve.
//
// Todo sale de Do menor natural (C D Eb F G Ab Bb), la escala del export. Las
// voces van en registros separados y la tabla de acordes nunca las cruza, asi
// que el reparto grave-medio-agudo se mantiene siempre.
//
// Estan abajo a proposito: el registro de tono del PSG es un divisor, asi que
// cuanto mas grave la nota mas fina es su resolucion -- aqui un paso vale
// entre 1,8 y 6,1 centesimas. El PSG tampoco baja de ~109 Hz, y A#2 a 117 es
// lo mas grave que se usa.
#define SLOP_NOTES_MAX 6
static const u16 slopVoiceHz[SLOP_VOICES][SLOP_NOTES_MAX] = {
    { 117, 131, 147, 175,   0,   0 }, // A#2 C3  D3  F3
    { 175, 196, 233, 262, 294,   0 }, // F3  G3  A#3 C4  D4
    { 233, 262, 294, 311, 349, 392 }, // A#3 C4  D4  Eb4 F4  G4
};
static const u8 slopVoiceCount[SLOP_VOICES] = { 4, 5, 6 };

// La S de sus curvas: logistica de k = 14,3, 33 puntos de 0 a 255.
static const u8 slopSigmoid[33] = {
      0,   0,   0,   1,   1,   2,   3,   4,   7,  11,  16,
     24,  36,  53,  74,  99, 127, 156, 181, 202, 219, 231,
    239, 244, 248, 251, 252, 253, 254, 254, 255, 255, 255,
};

// Volumen. El PSG va al reves que el FM y que SND_PCM4_setVolume: es una
// ATENUACION de 4 bits donde 0 es lo mas fuerte y 15 el silencio, 2 dB por
// paso. SLOP_ATT_FLOOR es lo mas fuerte que llega a sonar el acorde cuajado y
// SLOP_ATT_JITTER lo que cada voz se queda por debajo, para que la triada no
// este perfectamente equilibrada.
// Subido de 2 a 4 (el usuario: "bajale un poco el volumen, pero un poco, no
// mucho"). Cada paso del PSG son 2 dB, asi que son 4 dB justos.
#define SLOP_ATT_FLOOR  4
#define SLOP_ATT_JITTER 2 // 0..1
#define SLOP_ATT_SILENT 15

// El crescendo (peticion del usuario: "que el volumen de las primeras notas
// sea mas bajo y que progresivamente sube cuando se van sumando
// instrumentos"). Dos cosas: las voces entran de una en una y encima hay una
// atenuacion extra que arranca en SLOP_FADE_START y baja hasta 0 por
// habitacion, deslizandose un paso cada SLOP_FADE_FRAMES frames. Afecta a
// todas las voces vivas, asi que crece el conjunto. Aqui son pocos pasos
// porque cada uno vale 2 dB, el doble que en FM.
#define SLOP_FADE_START      3
#define SLOP_FADE_PER_ROOM   1
#define SLOP_FADE_FRAMES    20

// Ataque y cola. El PSG no tiene envolvente: escribir el volumen ES la
// envolvente, asi que la rampa la lleva este codigo. Sin esto una voz APARECE
// en un frame, que sobre un acorde sostenido se oye como un golpe, y al morir
// se corta en seco. Un paso cada N frames, y cada paso son 2 dB.
#define SLOP_ATTACK_FRAMES 10
#define SLOP_FADEOUT_FRAMES 6

// Movimiento continuo, en CENTESIMAS de semitono. SE QUITO EL VIBRATO RAPIDO:
// el registro de tono del PSG es un divisor y con este voicing un paso vale
// entre 2 y 3,6 centesimas, asi que un vibrato de +-10 centesimas son cinco
// valores discretos -- un tremolo escalonado, no una onda. Era lo que mas
// aspereza metia en reposo. Lo que queda es una deriva muy lenta y poco
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

// Las tres voces suenan desde la primera habitacion: con una progresion por
// medio, un acorde a medias no es un acorde. Lo que sigue entrando poco a poco
// es el VOLUMEN, que es de lo que iba la peticion del crescendo.
#define SLOP_VOICES_AT_START SLOP_VOICES

// Cada nota de cada voz como valor del registro del PSG. El registro es el
// divisor: f = clock / (32 * reg).
static u16 slopNoteTone[SLOP_VOICES][SLOP_NOTES_MAX];
static u16 slopHomeTone[SLOP_VOICES]; // la del primer acorde de la progresion
static u8 slopDriftStep[SLOP_VOICES]; // pasos de registro = SLOP_DRIFT_CENTS

// LA PROGRESION. Peticion del usuario: "los cambios notables que sean al
// cambiar de habitacion, y que tenga algo de musicalidad".
//
// Cada cruce de habitacion MUEVE el acorde al siguiente y ahi se queda hasta
// el cruce siguiente, asi que cada habitacion tiene su armonia y el cambio
// ocurre justo al entrar. Antes los gestos salian de la nota y volvian a
// ella, con una espera aleatoria por delante: el acorde era siempre el mismo
// y el movimiento no se asociaba a la puerta.
//
// Cada fila son los indices en slopVoiceHz de las tres voces. Indices y no
// frecuencias: asi es imposible pedir una nota que no este en la lista de esa
// voz, o sea fuera de la escala.
//
// La progresion NO esta elegida a gusto. Se enumeraron los 32 acordes que (a)
// son un subconjunto de Do menor natural, (b) tienen las tres voces en orden
// sin cruzarse y (c) forman una armonia reconocible, y de ahi se busco el
// ciclo de ocho que maximiza variedad de raices y de armonias penalizando el
// movimiento. Sale con 8 armonias distintas, 4 raices distintas, 22 semitonos
// de recorrido en todo el ciclo y ninguna voz saltando mas de 5 semitonos.
//
// Ese tope de 5 importa y no es estetico: el deslizamiento tiene un limite de
// centesimas por frame, asi que un salto de 7 semitonos tardaria 4,2 segundos
// y ya no se percibe como consecuencia de haber cruzado la puerta. Con 5 el
// mas largo son 3 segundos.
//
// Va en orden y cicla: una progresion que se repite se aprende, y eso es la
// musicalidad que se pedia. Sortear el orden seria cambiar una linea en
// slopAdvanceChord.
#define SLOP_CHORDS 8
static const u8 slopChord[SLOP_CHORDS][SLOP_VOICES] = {
    //  v1 v2 v3    notas           grado
    { 1, 1, 2 }, // C3  G3  D4      i add9
    { 0, 1, 1 }, // A#2 G3  C4      i7
    { 0, 0, 2 }, // A#2 F3  D4      VII  (Bb)
    { 1, 0, 3 }, // C3  F3  Eb4     iv7  (Fm7)
    { 1, 1, 3 }, // C3  G3  Eb4     i    (Cm)
    { 3, 1, 1 }, // F3  G3  C4      isus
    { 2, 1, 0 }, // D3  G3  A#3     v    (Gm)
    { 0, 0, 1 }, // A#2 F3  C4      VII9 (Bb9)
};
static u8 slopChordIdx = 0;

// Estado del deslizamiento de una voz hacia su nota nueva.

static u8 voiceLiveCount = 0; // las voces entran de una en una, ver Sfx_slop
static u8 voiceAtt[SLOP_VOICES];     // atenuacion objetivo, sin crescendo
static u8 voiceAttNow[SLOP_VOICES];  // la que esta puesta, para la rampa
static u8 voiceAttWritten[SLOP_VOICES];
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

// d pasos sobre un divisor "tone" valen 1200*log2((tone+d)/tone), que para d
// pequeno es 1731*d/tone centesimas (1731 = 1200/ln 2). Al reves: d =
// centesimas*tone/1731. Minimo 1, porque el registro es entero.
static u8 slopStepsForCents(u16 cents, u16 tone)
{
    const u16 steps = (u16) (((u32) cents * tone) / 1731u);

    return (u8) (steps ? steps : 1);
}

// El reloj del PSG no es el mismo en las dos normas, asi que las tablas se
// calculan al arrancar en vez de hornearlas.
static void slopInitTones(void)
{
    const u32 clock = IS_PAL_SYSTEM ? 3546893u : 3579545u;
    u8 v, i;

    for (v = 0; v < SLOP_VOICES; v++)
    {
        for (i = 0; i < slopVoiceCount[v]; i++)
            slopNoteTone[v][i] = (u16) (clock / (slopVoiceHz[v][i] * 32u));

        slopHomeTone[v] = slopNoteTone[v][slopChord[0][v]];
        // La deriva se mide sobre la nota de casa. El registro de una voz
        // cambia como mucho en un factor de 1,5 a lo largo de su lista, asi
        // que la profundidad real varia algo entre acordes -- da igual para
        // 8 centesimas.
        slopDriftStep[v] = slopStepsForCents(SLOP_DRIFT_CENTS, slopHomeTone[v]);
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

// Lo llama Sfx_loadDriver. El PSG no necesita nada del Z80 ni del YM2612, asi
// que aqui no hay orden que respetar: solo las tablas y dejarlo callado.
static void slopInit(void)
{
    slopInitTones();
    Sfx_slopStop();
    // slopStop pide desvanecer; al arrancar se quiere silencio ya.
    {
        u8 v;

        for (v = 0; v < SLOP_VOICES; v++)
            PSG_setEnvelope(v, SLOP_ATT_SILENT);
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
        voiceAtt[v] = SLOP_ATT_SILENT;
    }
    slopChordIdx = 0; // la proxima partida vuelve a empezar por la casa
    // La proxima partida vuelve a entrar desde abajo y de una voz en una.
    fadeNow = SLOP_FADE_START;
    fadeTarget = SLOP_FADE_START;
    fadeTick = 0;
}

// Mueve las tres voces al acorde siguiente de la progresion. Cada una se
// desliza desde donde este, no desde su nota de casa: si entra un cruce
// mientras todavia se desliza, el nuevo arranca de su posicion real.
static void slopAdvanceChord(void)
{
    u8 v;

    slopChordIdx = (u8) ((slopChordIdx + 1) % SLOP_CHORDS);

    for (v = 0; v < SLOP_VOICES; v++)
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
        u8 want = (voiceLiveCount == 0) ? SLOP_VOICES_AT_START
                                        : (u8) (voiceLiveCount + 1);

        if (want > SLOP_VOICES)
            want = SLOP_VOICES;

        while (voiceLiveCount < want)
        {
            const u8 nv = voiceLiveCount;

            voiceAtt[nv] = (u8) (SLOP_ATT_FLOOR + (slopNext() % SLOP_ATT_JITTER));
            voiceAttNow[nv] = SLOP_ATT_SILENT; // entra subiendo, ver Sfx_update
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
            if (voiceAtt[v] == SLOP_ATT_SILENT)
                voiceAtt[v] = (u8) (SLOP_ATT_FLOOR + (slopNext() % SLOP_ATT_JITTER));
    }

    // El acorde siguiente. En la primera habitacion de la partida no, porque
    // las voces acaban de nacer ya en el acorde de casa: deslizarlas en ese
    // mismo frame seria un cambio que el jugador no ha provocado.
    if (!started)
        slopAdvanceChord();

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
            PSG_setEnvelope(v, SLOP_ATT_SILENT);
        voiceLiveCount = 0;
        return;
    }

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

        // Rampa de volumen: un paso cada N frames, hacia arriba o hacia abajo.
        // Es lo que hace de envolvente, porque el PSG no trae ninguna.
        {
            const u8 every = slopReleasing ? SLOP_FADEOUT_FRAMES : SLOP_ATTACK_FRAMES;
            const u8 target = (u8) ((want > SLOP_ATT_SILENT) ? SLOP_ATT_SILENT : want);

            if (voiceAttNow[v] != target)
            {
                voiceRampTick[v]++;
                if (voiceRampTick[v] >= every)
                {
                    voiceRampTick[v] = 0;
                    voiceAttNow[v] = (u8) (voiceAttNow[v] > target ? voiceAttNow[v] - 1
                                                                   : voiceAttNow[v] + 1);
                }
            }
        }
        att = voiceAttNow[v];

        // Ya mudo y desvaneciendo: solo queda asegurar la escritura.
        if (slopReleasing && (att >= SLOP_ATT_SILENT))
        {
            if (voiceAttWritten[v] != att)
            {
                voiceAttWritten[v] = att;
                PSG_setEnvelope(v, att);
            }
            silent++;
            continue;
        }

        // La nota (o el deslizamiento hacia ella) con la deriva lenta encima.
        tone = slopVoiceTone(v);
        tone = (u16) (tone + ((slopLfo(voiceDriftPhase[v]) * slopDriftStep[v]) >> 6));
        voiceDriftPhase[v] = (u8) (voiceDriftPhase[v] + voiceDriftRate[v]);

        PSG_setTone(v, tone);
        if (voiceAttWritten[v] != att)
        {
            voiceAttWritten[v] = att;
            PSG_setEnvelope(v, att);
        }
    }

    // Desvanecido terminado: se cierra aqui y no en Sfx_slopStop, porque la
    // rampa necesita que las voces sigan vivas para poder bajarlas. Sin esto
    // la partida siguiente arrancaria con el acorde ya montado en vez de
    // construirlo voz a voz.
    if (slopReleasing && (silent >= voiceLiveCount))
        voiceLiveCount = 0;
}
