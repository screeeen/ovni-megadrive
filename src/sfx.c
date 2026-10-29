#include "sfx.h"
#include "resources.h"

static bool enabled = TRUE; // user request: "sonido es ON por defecto" (it used to start off)

// Lo del visor de depuracion, aqui arriba porque las funciones que disparan
// las muestras son las primeras del fichero. Ver el inventario del final.
static u8 sfxShotLeft[SFX_ID_SHOTS];
static u8 sfxSolo = SFX_ID_NONE;

// Una muestra a 16 kHz dura len/16000 s, o sea len/266 frames en NTSC. El
// driver PCM4 no sabe decir si una muestra sigue sonando, asi que el visor
// pinta esta cuenta atras y no una verdad.
//
// Con un SUELO de 12 frames (0,2 s), que no es cosmetico: TYPE dura 18 ms, o
// sea UN frame, y un parpadeo de un frame no se ve. El resto si llegan solos
// (KICK 36 frames, LETTER 19, HAT 10; KILL ya no es muestra y dura sus 16
// frames de PSG). O sea que para las muestras la
// barra dice "acaba de sonar" y no "esta sonando"; para las voces FM y el
// siseo, que son sostenidas, si dice el nivel de verdad.
#define SFX_SHOT_MIN_FRAMES 12
#define SFX_SHOT_FRAMES(len) \
    ((u8) ((((len) / 266u) + 1u) < SFX_SHOT_MIN_FRAMES \
             ? SFX_SHOT_MIN_FRAMES : (((len) / 266u) + 1u)))


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

// Las muestras y sus tamanos, aqui arriba porque las tocan DOS sitios: el
// juego (el muro y las plantas) y la percusion del jazz, que lleva las SUYAS:
// el bombo del muro esta afilado (110 ms) y el del jazz es el largo de siempre
// (300 ms) con su nivel ya cocido -- ver tools/kick_sharpen.py.
static const u8 *const kicks[4] = { kick0, kick1, kick2, kick3 };
static const u32 kickLens[4] = {
    sizeof(kick0), sizeof(kick1), sizeof(kick2), sizeof(kick3)
};
static const u8 *const jazzKicks[2] = { jazzKick0, jazzKick1 };
static const u32 jazzKickLens[2] = { sizeof(jazzKick0), sizeof(jazzKick1) };
static const u8 *const hihats[4] = { hihat0, hihat1, hihat2, hihat3 };
static const u32 hihatLens[4] = {
    sizeof(hihat0), sizeof(hihat1), sizeof(hihat2), sizeof(hihat3)
};

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

// El canal de todo lo corto.
#define SFX_SHORT_CHANNEL SOUND_PCM_CH4

// --- LA MUSICA DE LA PARTIDA -----------------------------------------------
//
// Las siete canciones de res/music, por el driver XGM. No convive con el PCM4,
// asi que se cambia de driver al entrar y al salir de la partida; cada cambio
// resetea el YM, y por eso el ambiente se reescribe entero detras.
//
// Las muestras van por XGM_startPlayPCM con ids >= 64 (de 1 a 63 son de la
// cancion) en los canales 2 y 3 el bombo y el 4 lo corto. Las canciones usan
// los cuatro a prioridad 0: la nuestra es 1, asi que gana y le come el golpe
// que tocase. El driver XGM no tiene volumen por canal -- el nivel va cocido
// en el wav.
//
// UNA CANCION POR PLANETA (peticion del usuario). El indice es el del planeta
// y NO la fila sorteada de menuPlanet[]: el sonido del menu se reparte de nuevo
// en cada vuelta pero la cancion no, asi que el planeta 3 suena igual en todas
// las partidas.
#define MUSIC_TRACKS 7  // siete canciones para ocho planetas: el 7 repite la 0
#define MENU_PLANETS 8
#if MUSIC_TRACKS > MENU_PLANETS
#error "planetMusic[] tiene mas canciones que planetas"
#endif

static const u8 *const planetMusic[MUSIC_TRACKS] = {
    musPattern, musAventura, musHouse1,
    musHouse2, musHouse3, musHouse4, musHouse5
};

// Para el visor: lo que suena ahora, en la fila MUSIC del inventario.
static const char *const planetMusicName[MUSIC_TRACKS] = {
    "pattern", "aventura", "house1",
    "house2", "house3", "house4", "house5"
};

// Que cancion le toca a cada fase. Era el indice de la fase tal cual; ahora es
// una tabla para poder cambiarle la cancion a una sola sin tocar a las demas
// (peticion del usuario: otra para la fase 2). La 2 y la 7 estan cambiadas
// entre si, asi que las siete canciones se siguen usando todas.
static const u8 planetTrack[MENU_PLANETS] = {
    0,  // fase 1  pattern
    6,  // fase 2  house5   (era aventura)
    2,  // fase 3  house1
    3,  // fase 4  house2
    4,  // fase 5  house3
    5,  // fase 6  house4
    1,  // fase 7  aventura (era house5)
    0,  // fase 8  pattern
};

static u8 musicTrack = 0; // lo apunta Sfx_setPlanet

#define XGM_SMP_KICK0    64  // 64..67, las cuatro variantes
#define XGM_SMP_HAT0     68  // 68..71
#define XGM_SMP_LETTER   73
#define XGM_SMP_TYPE     74
#define XGM_SMP_JKICK0   75  // 75..76, los dos bombos del jazz
#define XGM_SMP_JRIDE0   77  // 77..80, los cuatro platos
#define XGM_SFX_PRIORITY 1

static bool musicOn = FALSE;

// LA CANCION SE PAUSA, NO SE PARA (peticion del usuario: "conserva la musica
// en el estado en el que estaba cuando mueres y vuelves a la fase"). Parar
// descarga el driver XGM, y con el se va por donde iba la cancion: no hay
// forma de volver a ese punto, XGM no sabe buscar dentro de un tema. Asi que
// al salir de la partida se pausa y el driver se queda puesto, y al volver a
// la misma cancion se reanuda donde estaba. Lo que cuesta es que todo lo que
// suena por el camino -- el jazz del game over y el tic del typewriter --
// tiene que salir por XGM, que es lo que hacen sus ramas de musicOn.
static bool musicPaused = FALSE;
static u8 pausedTrack = 0;

// Si alguien ha reprogramado las voces FM desde que se pauso la cancion. Lo
// hacen el ambiente del menu y el jazz del game over, que usan las MISMAS
// cinco voces que la musica: reanudar despues de eso deja al tema tocando con
// los timbres del otro hasta que vuelva a mandar instrumento, y eso es el
// pitido que se oia (reporte del usuario). Cuando pasa, la cancion se
// reinicia en vez de reanudarse -- que suene desde el principio es mejor que
// que suene mal. El YM2612 es de solo escritura, asi que no hay forma de
// guardar y devolver los registros.
static bool fmDirty = FALSE;

static void musicStop(void);

static void musicPause(void)
{
    if (!musicOn || musicPaused)
        return;

    XGM_pausePlay();
    musicPaused = TRUE;
    pausedTrack = musicTrack;
}

static void musicStart(void)
{
    if (!enabled)
        return;

    if (musicOn)
    {
        if (!musicPaused)
            return;
        if ((pausedTrack == musicTrack) && !fmDirty)
        {
            XGM_resumePlay();   // la misma cancion y el FM intacto: sigue por donde iba
            musicPaused = FALSE;
            return;
        }
        musicStop();            // otra cancion, o el FM pisado: de cero
    }

    XGM_startPlay(planetMusic[musicTrack]); // carga el driver XGM y resetea los dos chips
    musicOn = TRUE;
    fmDirty = FALSE;   // el driver acaba de reprogramar el chip entero

    // Despues de arrancar: el driver recien cargado borra su RAM, y
    // XGM_startPlay solo sube la tabla del tema (ids 1..63). Con las
    // interrupciones fuera: la tarea de vblank del driver XGM pide el bus por
    // su cuenta y lo SUELTA, y a media rafaga eso escribe sobre un Z80 vivo.
    SYS_disableInts();
    Z80_requestBus(TRUE);
    XGM_setPCMFast(XGM_SMP_KICK0 + 0, xgmKick0, sizeof(xgmKick0));
    XGM_setPCMFast(XGM_SMP_KICK0 + 1, xgmKick1, sizeof(xgmKick1));
    XGM_setPCMFast(XGM_SMP_KICK0 + 2, xgmKick2, sizeof(xgmKick2));
    XGM_setPCMFast(XGM_SMP_KICK0 + 3, xgmKick3, sizeof(xgmKick3));
    XGM_setPCMFast(XGM_SMP_HAT0 + 0, xgmHihat0, sizeof(xgmHihat0));
    XGM_setPCMFast(XGM_SMP_HAT0 + 1, xgmHihat1, sizeof(xgmHihat1));
    XGM_setPCMFast(XGM_SMP_HAT0 + 2, xgmHihat2, sizeof(xgmHihat2));
    XGM_setPCMFast(XGM_SMP_HAT0 + 3, xgmHihat3, sizeof(xgmHihat3));
    XGM_setPCMFast(XGM_SMP_LETTER, xgmLetterPickup, sizeof(xgmLetterPickup));
    // Las de fuera de la partida: sin estas habria que descargar el driver
    // XGM para que sonaran, y con el se va por donde iba la cancion.
    XGM_setPCMFast(XGM_SMP_TYPE, xgmTypeTick, sizeof(xgmTypeTick));
    XGM_setPCMFast(XGM_SMP_JKICK0 + 0, xgmJazzKick0, sizeof(xgmJazzKick0));
    XGM_setPCMFast(XGM_SMP_JKICK0 + 1, xgmJazzKick1, sizeof(xgmJazzKick1));
    XGM_setPCMFast(XGM_SMP_JRIDE0 + 0, xgmJazzRide0, sizeof(xgmJazzRide0));
    XGM_setPCMFast(XGM_SMP_JRIDE0 + 1, xgmJazzRide1, sizeof(xgmJazzRide1));
    XGM_setPCMFast(XGM_SMP_JRIDE0 + 2, xgmJazzRide2, sizeof(xgmJazzRide2));
    XGM_setPCMFast(XGM_SMP_JRIDE0 + 3, xgmJazzRide3, sizeof(xgmJazzRide3));
    Z80_releaseBus();
    SYS_enableInts();
}

static void musicStop(void)
{
    if (!musicOn)
        return;

    XGM_stopPlay();
    musicOn = FALSE;
    musicPaused = FALSE;
    Sfx_loadDriver(); // el PCM4 de vuelta, con su reset del YM
}

void Sfx_loadDriver(void)
{
    SND_PCM4_loadDriver(TRUE); // deja el latch del YM en $2A, ver fmEnd
}

void Sfx_setEnabled(bool value)
{
    enabled = value;
    if (!value)
        Sfx_musicStop(); // la C del menu calla tambien el acorde sostenido
}

bool Sfx_isEnabled(void)
{
    return enabled;
}

void Sfx_playWallHit(u16 distance)
{
    const SoundPCMChannel channel = (SoundPCMChannel) nextKickChannel;
    u16 vol = SFX_VOL_MIN + (distance / SFX_VOL_DIST_PER_LEVEL);

    if (!enabled)
        return;

    if (vol > SFX_VOL_MAX)
        vol = SFX_VOL_MAX;

    if (musicOn)
        // La distancia no se oye: el driver XGM no tiene volumen por canal.
        XGM_startPlayPCM((u8) (XGM_SMP_KICK0 + nextKickVariant), XGM_SFX_PRIORITY,
                         (SoundPCMChannel) (SOUND_PCM_CH2 + (nextKickVariant & 1)));
    else
    {
        // Set before starting playback so the very first mixed sample of this
        // hit already uses the new level, not whatever this channel was left
        // at by its previous hit.
        SND_PCM4_setVolume(channel, (u8) vol);
        SND_PCM4_startPlay(kicks[nextKickVariant], kickLens[nextKickVariant], channel, FALSE);
    }
    sfxShotLeft[SFX_ID_KICK] = SFX_SHOT_FRAMES(kickLens[nextKickVariant]);
    nextKickVariant = (nextKickVariant + 1) & 3;
    nextKickChannel = (nextKickChannel == 2) ? 0 : (u8) (nextKickChannel + 1);
}

void Sfx_playPlantPickup(void)
{
    if (!enabled)
        return;

    if (musicOn)
        XGM_startPlayPCM((u8) (XGM_SMP_HAT0 + nextHihatVariant), XGM_SFX_PRIORITY,
                         SFX_SHORT_CHANNEL);
    else
    {
        SND_PCM4_setVolume(SFX_SHORT_CHANNEL, SFX_VOL_MAX);
        SND_PCM4_startPlay(hihats[nextHihatVariant], hihatLens[nextHihatVariant], SFX_SHORT_CHANNEL, FALSE);
    }
    sfxShotLeft[SFX_ID_HAT] = SFX_SHOT_FRAMES(hihatLens[nextHihatVariant]);
    nextHihatVariant = (nextHihatVariant + 1) & 3;
}

// Placeholders -- see their doc comments in sfx.h. Synthesized at the
// same mono/16-bit/16kHz the other samples use:
//   enemyKill    un splash: ruido con el brillo cayendo de 4200 a 1100 Hz,
//                chasquido ancho y golpe corto debajo (~300 ms), en seis
//                pitches que rotan. Lo hace tools/enemykill_splash.py, el
//                zap de antes esta en res/sound/enemykill_src0.wav
//   letterPickup two square blips a fifth apart, A5 then E6 (~160 ms)
#define SFX_VOL_TYPE_TICK 2 // lo mas bajo que sigue oyendose, ver sfx.h

void Sfx_playTypeTick(void)
{
    // Con la musica puesta no suena: no tiene copia para el driver XGM, y las
    // pantallas que escriben solas estan todas fuera de la partida.
    if (!enabled)
        return;

    // Canal fijo, no AUTO: asi un tick corta al anterior en vez de
    // apilarse con el. Si dos llegan muy seguidos se quiere uno, no dos
    // sonando a la vez.
    if (musicOn)
        XGM_startPlayPCM(XGM_SMP_TYPE, XGM_SFX_PRIORITY, SFX_SHORT_CHANNEL);
    else
    {
        SND_PCM4_setVolume(SFX_SHORT_CHANNEL, SFX_VOL_TYPE_TICK);
        SND_PCM4_startPlay(typeTick, sizeof(typeTick), SFX_SHORT_CHANNEL, FALSE);
    }
    sfxShotLeft[SFX_ID_TYPE] = SFX_SHOT_FRAMES(sizeof(typeTick));
}

// --- EL SPLASH, DEL CHIP ---------------------------------------------------
//
// Matar un enemigo ya no es una muestra sino el PSG (peticion del usuario: "me
// gustaria que todo fuese desde el chip"): el canal de ruido relojado por el
// divisor del canal de tono 2 -- lo mismo que hace el cepillo del jazz -- y el
// canal de tono 0 por debajo haciendo de golpe, para que aterrice en algo.
//
// Asi el pitch sale distinto en CADA muerte de verdad y no de una tabla de
// seis muestras: el divisor del ruido se sortea al golpear y de ahi sube, que
// es el brillo cayendo del splash. De paso se van los 55 KB de ROM que
// ocupaban las doce muestras, seis del PCM4 y seis del XGM.
//
// LOS TRES CANALES ESTAN LIBRES: ninguna de las siete canciones escribe el
// ruido ni los tonos 0 y 2, solo los inicializa al silencio -- contado sobre
// el stream XGM, 2 latches por canal en las siete. Y NO se le pide el bus al
// Z80 para escribirlos aunque el driver toque el PSG por su cuenta: la unica
// carrera que queda es colarse entre un latch suyo y su dato, que desafina una
// nota un instante y sale menos de una vez por cada cien muertes, mientras que
// pararle el bus un rato cada frame le come una muestra al PCM y eso cruje
// siempre.
#define SPLASH_FRAMES      16  // 266 ms, los que tarda la envolvente en caer
#define SPLASH_DIV_MIN     10  // divisor del reloj del ruido al golpear...
#define SPLASH_DIV_SPAN     6  // ...sorteado en MIN..MIN+SPAN, que son 8 semitonos
                               // de un golpe a otro; mas divisor, mas grave
#define SPLASH_BODY_DIV   589  // el golpe empieza en 190 Hz...
#define SPLASH_BODY_STEP   86  // ...y baja hasta 110 Hz, que es lo mas grave
#define SPLASH_BODY_FRAMES  5  // que el PSG sabe dar
#define SPLASH_BODY_FALL    3  // pasos de atenuacion por frame del golpe
#define SPLASH_BODY_ATT     2  // y arranca por debajo del ruido: es un apoyo

static u8  splashLeft;  // frames que le quedan al splash, 0 = callado
static u16 splashDiv;   // divisor del reloj del ruido ahora mismo
static u16 splashBody;  // y el del golpe de abajo

// Sorteo propio y no random(): ese stream se lo reservan los generadores del
// mapa y de las salas, que dependen de pedirle los numeros siempre en el mismo
// orden (misma razon que los indices rotatorios de mas arriba).
static u16 splashSeed = 0xACE1;

static u16 splashRandom(void)
{
    splashSeed ^= (u16) (splashSeed << 7);
    splashSeed ^= (u16) (splashSeed >> 9);
    splashSeed ^= (u16) (splashSeed << 8);
    return splashSeed;
}

static void splashOff(void)
{
    splashLeft = 0;
    PSG_setEnvelope(0, PSG_ENVELOPE_MIN);
    PSG_setEnvelope(3, PSG_ENVELOPE_MIN);
}

static void splashHit(void)
{
    splashDiv = (u16) (SPLASH_DIV_MIN + (splashRandom() % (SPLASH_DIV_SPAN + 1)));
    splashBody = SPLASH_BODY_DIV;
    splashLeft = SPLASH_FRAMES;

    PSG_setNoise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_TONE3);
    PSG_setTone(2, splashDiv);
    PSG_setEnvelope(3, PSG_ENVELOPE_MAX);
    PSG_setTone(0, splashBody);
    PSG_setEnvelope(0, SPLASH_BODY_ATT);
}

// Un frame de splash. El divisor del ruido sube un 6% y pico por frame, que en
// los 16 que dura son las dos octavas de caida de brillo que tenia el filtro
// de la muestra; la envolvente del golpe baja tres veces mas rapido que la del
// ruido, porque es un golpe y no una cola.
static void splashTick(void)
{
    const u8 frame = (u8) (SPLASH_FRAMES - splashLeft);

    splashDiv = (u16) (splashDiv + (splashDiv >> 4) + 1);
    PSG_setTone(2, splashDiv);
    PSG_setEnvelope(3, (frame < PSG_ENVELOPE_MIN) ? frame : PSG_ENVELOPE_MIN);

    if (frame < SPLASH_BODY_FRAMES)
    {
        const u8 bodyAtt = (u8) (SPLASH_BODY_ATT + (frame * SPLASH_BODY_FALL));

        splashBody = (u16) (splashBody + SPLASH_BODY_STEP);
        PSG_setTone(0, splashBody);
        PSG_setEnvelope(0, (bodyAtt < PSG_ENVELOPE_MIN) ? bodyAtt : PSG_ENVELOPE_MIN);
    }
    else if (frame == SPLASH_BODY_FRAMES)
        PSG_setEnvelope(0, PSG_ENVELOPE_MIN);

    splashLeft--;
    if (!splashLeft)
        splashOff();
}

void Sfx_playEnemyKill(void)
{
    if (!enabled)
        return;

    // El PSG suena igual con la cancion puesta o sin ella, asi que aqui no hay
    // la rama de musicOn que llevan las muestras.
    splashHit();
    sfxShotLeft[SFX_ID_KILL] = SPLASH_FRAMES;
}

void Sfx_playLetterPickup(void)
{
    if (!enabled)
        return;

    if (musicOn)
        XGM_startPlayPCM(XGM_SMP_LETTER, XGM_SFX_PRIORITY, SFX_SHORT_CHANNEL);
    else
    {
        SND_PCM4_setVolume(SFX_SHORT_CHANNEL, SFX_VOL_MAX);
        SND_PCM4_startPlay(letterPickup, sizeof(letterPickup), SFX_SHORT_CHANNEL, FALSE);
    }
    sfxShotLeft[SFX_ID_LETTER] = SFX_SHOT_FRAMES(sizeof(letterPickup));
}


// --- LA CAPA DE ESCRITURA DEL CHIP ------------------------------------------
//
// Lo unico que escribe el YM2612 hoy es el jazz del game over. Esta capa es
// suya: la cache de lo ultimo escrito, las mascaras de que falta por escribir
// y la rafaga que lo manda, todo en una sola toma de bus por frame.
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
// Del PSG se usan el canal de tono 2 (callado, solo como reloj del ruido) y el
// canal de ruido, para el cepillo del jazz. Los canales 0 y 1 no se tocan. Ese
// canal de ruido lo comparte con la estatica del visor de mapa, y nunca
// coinciden: el visor es pantalla de partida y el jazz es el game over.

#define SLOP_VOICES 5


// --- LOS CANALES DEL FM ----------------------------------------------------
//
// Cinco canales FM. Los tres primeros en la parte 0 (canales 1 a 3) y los dos
// ultimos en la parte 1 (canales 4 y 5); el 6 es del DAC y lo usa el driver
// PCM4 para el bombo.


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
#define SLOP_FADE_FRAMES     8


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


// Los parametros propios de la melodica.


// Y los de la sala de maquinas.


// EL MENU, UN SONIDO POR PLANETA. Peticion del usuario: "no se solapen los
// sonidos, cada planeta tiene el suyo". No se acumulan capas: seleccionar un
// planeta CORTA lo que hubiera y deja sonando una sola voz, la 0, reprogramada
// con esta fila. Lo que distingue un planeta de otro es la nota, el ritmo y el
// timbre, por ese orden de peso para el oido.
#define MAQ_PSG_OFF 16        // esta fila no lleva siseo (la escala llega a 15)

typedef struct {
    u16 hz10;       // la nota de reposo, en decimas de Hz
    u16 attackHz10; // de donde cae al golpear; 0 = sin caida de tono
    u16 period;     // frames entre golpes; 0 = sostenida
    u8 block;       // el fnum son 11 bits: por encima de 208 Hz hace falta el 1
    u8 modTL, modMul, modDT, fb, pms;
    u8 tl;          // su nivel; SLOP_TL_QUIET es inaudible y deja la voz viva
    u8 psgAtt;      // atenuacion del siseo, o MAQ_PSG_OFF
} MenuVoice;


typedef struct {
    u8  beat;     // frames por pulso: el tempo
    u8  prog;     // que progresion de melProg[]
    u8  oct;      // octavas que baja el pad entero (0, 1)
    u8  modTL;    // indice del pad: MENOS es MAS brillante/sucio
    u8  melTL;    // indice de la melodia
    u8  mul;      // MUL del modulador del pad
    u8  dt;       // detune del modulador: la suciedad
    u8  fb;       // realimentacion del bajo: hacia sierra
    u8  pms;      // temblor de tono del LFO
    u8  sig;      // MEL_SIG_*
    u16 sigHz10;  // la nota de la firma, en decimas de Hz
    u8  sigBeats; // cada cuantos pulsos golpea; 0 = sostenida
    u8  sigTL;    // su nivel
    u8  psgAtt;   // el siseo del PSG, o MAQ_PSG_OFF
} MelPlanet;



// Estado del deslizamiento de una voz hacia su nota nueva.

static u8 voiceAttNow[SLOP_VOICES];  // la que esta puesta, para la rampa
static u8 voiceAttWritten[SLOP_VOICES];
static u16 voiceToneWritten[SLOP_VOICES];
static u8 voiceDriftRate[SLOP_VOICES];
static u8 voiceDriftPhase[SLOP_VOICES];

static u16 voiceTone[SLOP_VOICES]; // registro donde la voz va a asentarse
// Donde esta AHORA mismo, que no es lo mismo: si entra un cruce mientras la
// voz todavia se desliza, el deslizamiento nuevo tiene que salir de aqui.
// Usar voiceTone daba saltos de hasta 15 semitonos en un frame, porque
// voiceTone ya valia el destino del deslizamiento en vuelo.
static u16 voiceCur[SLOP_VOICES];
static u16 gTimer[SLOP_VOICES];    // frames que quedan de deslizamiento; 0 = quieta
static u8 slopDriftStep[SLOP_VOICES]; // pasos de registro = SLOP_DRIFT_CENTS

static bool slopReleasing = FALSE; // Sfx_slopStop pidio desvanecer

static void slopVoiceTimbre(u8 v, u8 modTL, u8 modMul, u8 modDT, u8 fb, u8 pms);
static void psgNoiseStop(void);

// El jazz del game over vive en su propia seccion al final, pero tres cosas de
// el hacen falta aqui arriba: el block lo pide slopBlockOf, y el corte lo pide
// Sfx_slopStop, que es por donde se sale de la pantalla de game over.
static bool jazzOn = FALSE;
static u8 jazzBlockOf(u8 v);
static void jazzSilence(void);


// El fnum de una nota suelta. slopInitTones hace esto mismo para las tablas de
// las dos modalidades; el menu lo necesita nota a nota.
static u16 slopFnum(u16 hz10, u8 block)
{
    const u32 k = IS_PAL_SYSTEM ? 318u : 315u;

    return (u16) (((u32) hz10 * k) / (10u << (5 + block)));
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


// Lo que se le escribe al chip en un frame, montado antes de pedir el bus.
// Pedir el bus, escribir y soltarlo es una sola rafaga al final de Sfx_update:
// el Z80 tiene 223 ciclos por muestra y cada ida y vuelta le cuesta muestras.
static u16 wrTone[SLOP_VOICES];
static u8 wrTL[SLOP_VOICES];
static u8 wrToneMask = 0; // bit v: hay que escribir el tono de la voz v
static u8 wrTLMask = 0;

// El block de esta voz. Lo unico que escribe tonos es el jazz del game over.
static u8 slopBlockOf(u8 v)
{
    return jazzBlockOf(v);
}

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
                            (u8) ((slopBlockOf(v) << 3) | (wrTone[v] >> 8)));
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


// Lo de arriba, para una sola voz. Partido asi porque el menu reprograma la voz
// 0 en cada planeta.
static void slopVoiceTimbre(u8 v, u8 modTL, u8 modMul, u8 modDT, u8 fb, u8 pms)
{
    fmDirty = TRUE;   // ver donde se declara: la cancion ya no se puede reanudar
    static const u8 ops[4] = { SLOP_OP_S1, SLOP_OP_S3, SLOP_OP_S2, SLOP_OP_S4 };
    const u8 part = fmPart[v];
    const u8 c = fmCh[v];
    u8 o;

    {
        for (o = 0; o < 4; o++)
        {
            const u8 op = ops[o];

            Z80_requestBus(TRUE);
            // El modulador con su MUL y su DT; la portadora en MUL 1 y sin DT.
            YM2612_writeReg(part, (u8) (0x30 + op + c),
                            (op == SLOP_OP_S1)
                              ? (u8) ((modDT << 4) | modMul)
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
                    tl = modTL;
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
                        (u8) ((fb << 3) | 0x04));
        // Los dos canales de salida, y la profundidad de tono del LFO.
        YM2612_writeReg(part, (u8) (0xB4 + c), (u8) (0xC0 | pms));
        YM2612_writeReg(0, 0x28, (u8) (0xF0 | fmKey[v])); // key on, 4 slots
        YM2612_write(0, 0x2A); // la key on es de la parte 0 siempre
        Z80_releaseBus();
    }
}


// --- CALLARLO TODO ----------------------------------------------------------

// Calla los dos chips a lo bruto: key off en los seis canales del FM y los
// cuatro del PSG al minimo. Es la red de seguridad de Sfx_musicStop, porque no
// todo lo que suena pasa por aqui -- la estatica del mapa, por ejemplo, se
// queda el canal de ruido a tope y solo lo suelta al soltar la A, asi que
// cualquier salida que no sea esa lo dejaba pitando en el menu.
static void chipsSilence(void)
{
    u8 i;

    splashLeft = 0; // sin esto el tick volveria a encender el ruido al frame siguiente

    for (i = 0; i < 4; i++)
        PSG_setEnvelope(i, PSG_ENVELOPE_MIN);

    Z80_requestBus(TRUE);
    for (i = 0; i < 6; i++)
        YM2612_writeReg(0, 0x28, (u8) ((i < 3) ? i : (i + 1))); // key off, 0 slots
    YM2612_write(0, 0x2A); // el latch de la parte 0 en su sitio, ver slopFlush
    Z80_releaseBus();
}

// Calla lo que este sonando que no sea un efecto suelto. Se pasa por aqui al
// volver al menu, al acabar la partida y al apagar el sonido con la C.
// Deja la cancion donde esta y calla todo lo demas. Es lo que se usa al salir
// de la partida -- al menu o al game over -- para que al volver siga por donde
// iba; Sfx_musicStop, el de verdad, es para cuando ya da igual (fase acabada).
void Sfx_musicPause(void)
{
    musicPause();

    if (jazzOn)
        jazzSilence();

    chipsSilence();
}

void Sfx_musicStop(void)
{
    musicStop();

    // El jazz se corta en seco: al salir del game over al menu lo siguiente
    // reescribe el modulador de estas mismas voces.
    if (jazzOn)
        jazzSilence();

    chipsSilence();
}

// --- EL JAZZ DEL GAME OVER -------------------------------------------------
//
// Peticion del usuario: "quiero que suene jazz en game over". No es una
// modalidad mas del ambiente: es un CORTE, con principio y fin, que arranca
// gameOverBegin y se muere solo. Por eso no pasa por la escena -- ahi todo esta
// indexado por modalidad en tablas de dos filas y meter una tercera seria
// tocar veinte sitios para un gesto que dura seis segundos. Lo que si reutiliza
// es la fontaneria: slopFnum para las notas, slopVoiceTimbre para el timbre y
// slopFlush para escribir, o sea que el contrato del bus del Z80 (dejar el
// latch de la parte 0 en $2A antes de soltar) lo cumple igual que el ambiente.
//
// LA CADENCIA. Un recorrido en Do menor, que es la tonalidad en la que ya
// esta la partida. Trece acordes en tres frases, y se resuelve en el TERCERO a
// proposito: la pantalla de game over se abandona con la A en cuanto el
// jugador quiere, asi que lo que tiene que estar oido antes que nada es la
// resolucion. Lo que viene detras es propina -- pero propina larga, que es lo
// que pidio el usuario ("el jazz lo quiero mas largo"): doce segundos en vez
// de seis y medio.
//
//   la frase que resuelve:
//     1  Dm7b5    el ii medio disminuido
//     2  G7b9     el V, con la novena bemol que lo hace sonar a menor
//     3  Cm7      RESUELVE, y aqui ya se ha dicho lo importante
//     4  Ab9      el bVI, el giro
//     5  G7b9     el V otra vez
//     6  Cm7      y resuelve de nuevo, pero ahora sigue andando
//
//   el paseo por los grados bemoles, que es lo que alarga sin repetirse:
//     7  Fm7      el iv
//     8  Bb7      el bVII7
//     9  Ebmaj7   el bIII, el relativo mayor: aqui es donde abre
//    10  Abmaj7   el bVI
//
//   el cierre, mas tenso que la primera vez:
//    11  Dm7b5    el ii otra vez: cierra el circulo
//    12  G7#5     el V alterado, con la quinta aumentada
//    13  Cm9      resuelve y se queda sonando hasta apagarse
//
// LAS VOCES SE CONDUCEN, no se transportan: de un acorde al siguiente ninguna
// se mueve mas de un tono, y casi siempre hay alguna que se QUEDA QUIETA
// porque la nota que tenia tambien pertenece al acorde que llega (la de en
// medio aguanta el Lab del 6 al 8, la de abajo aguanta el Re del 8 al 9). Eso
// es lo que hace que suene a alguien tocandolo y no a trece acordes seguidos.
// La unica excepcion es a proposito: del 11 al 12 las dos de arriba suben una
// tercera menor a la vez, que es la tension abriendose antes del final, y del
// 12 al 13 las tres bajan un paso y se resuelve todo hacia abajo.
//
// EL BAJO ANDA. Dos notas por acorde en vez de una, la segunda a mitad de
// compas, que es lo que convierte tres acordes en un paseo: Re-Fa, Sol-Sib,
// Do-Mib, Lab-Sol, Sol-Sol, Do.
//
// Y la envolvente la lleva el TL, como todo en este archivo: la del chip esta
// puesta para sostener (AR 31, sin decaimiento) y no se toca. Cada golpe pone
// la voz en su nivel de ataque y de ahi sube de TL en TL hacia su reposo, que
// es lo que suena a cuerda pulsada en vez de a organo.
// LA OCTAVA Y EL PULSO. Peticion del usuario: "el jazz con una octava, que sea
// mas upbeat". Son las dos caras de lo mismo, porque lo que hacia que esto
// sonara a balada no era solo el tempo: era el REGISTRO. Trece acordes en la
// octava de Fa3 a Do4, graves y oscuros, con el bajo en 2 y el plato marcando
// medio compas.
//
//   LA OCTAVA es una voz MAS, la quinta, que dobla a la melodia una octava
//   arriba. Cabe porque el melodico usa 3 voces de 5 y el jazz usaba 4: la
//   quinta estaba libre (el canal 6 del chip es del driver PCM4 y no cuenta).
//   Doblar en octavas es lo que hace que una melodia suene brillante en vez de
//   gorda -- es el recurso de toda la vida, y aqui encima mete el acorde en una
//   octava donde el altavoz de un televisor radia de verdad.
//
//   EL PULSO sube de 21 frames a 18, de 171 a 200 negras por minuto, y el
//   plato pasa a tocar corcheas swing en LOS DOS pulsos y no solo en el
//   segundo. Eso es lo que se oye como upbeat: no el tempo, que sube un 17%,
//   sino que el ritmo no deje huecos.
#define JAZZ_VOICES  5
#define JAZZ_STEPS  13
#define JAZZ_BEAT   18   // 0,30 s: el bajo pisa cada una de estas
#define JAZZ_CHORD  36   // dos pulsos por acorde, o sea 100 compases por minuto
#define JAZZ_RING  240   // lo que el ultimo acorde se queda sonando: 4 s

// La voz 0 es el bajo y va en block 0; las tres de arriba en el 1. Un block
// por voz y no por nota a la fuerza: el deslizamiento interpola el registro en
// bruto, asi que las notas de una misma voz tienen que compartirlo. Comprobado
// que las trece notas caben en los 11 bits del fnum en NTSC y en PAL.
// La octava va en el block 2 a la fuerza: Mib5 son 622 Hz y en el block 1 el
// fnum se saldria de los 11 bits.
static const u8 jazzBlock[JAZZ_VOICES] = { 0, 1, 1, 1, 2 };

// Las tres de arriba, en decimas de Hz. Ver el dibujo de la conduccion arriba.
// Los acordes del paseo van sin fundamental: la tiene el bajo, asi que las tres
// voces pueden quedarse con el color (la septima, la tercera y la novena).
static const u16 jazzChord[JAZZ_STEPS][3] = {
    { 1746, 2077, 2616 }, //  1 Dm7b5   Fa3  Lab3 Do4
    { 1746, 2077, 2469 }, //  2 G7b9    solo baja la de arriba, Do -> Si
    { 1556, 1960, 2331 }, //  3 Cm7     las tres bajan un paso o menos
    { 1556, 1850, 2331 }, //  4 Ab9     solo baja la de en medio, Sol -> Fa#
    { 1746, 2077, 2469 }, //  5 G7b9    las tres suben otra vez
    { 1556, 1960, 2331 }, //  6 Cm7     y vuelven a resolver
    { 1556, 2077, 2616 }, //  7 Fm7     la de abajo AGUANTA el Mib (su septima)
    { 1468, 2077, 2616 }, //  8 Bb7     y ahora aguantan las dos de arriba
    { 1468, 1960, 2331 }, //  9 Ebmaj7  la de abajo aguanta el Re (su septima)
    { 1556, 1960, 2616 }, // 10 Abmaj7  y la de en medio el Sol (la suya)
    { 1746, 2077, 2616 }, // 11 Dm7b5   de vuelta al acorde del principio
    { 1746, 2469, 3111 }, // 12 G7#5    las dos de arriba suben una tercera
    { 1556, 2331, 2937 }, // 13 Cm9     todo baja un paso; el Re es el color
};

// La voz de la octava no tiene tabla: es la de arriba por dos, y asi no puede
// desafinarse de ella al tocar la progresion.
#define JAZZ_OCTAVE_VOICE 4
#define jazzOctaveHz10(step) ((u16) (jazzChord[step][2] * 2u))

// El bajo, dos por acorde. La segunda es la que anda hacia el acorde que viene,
// y de ahi que el paseo suba y baje por cuartas en vez de ir en linea recta.
static const u16 jazzBass[JAZZ_STEPS][2] = {
    {  734,  873 }, //  1 Re2  Fa2
    {  980, 1167 }, //  2 Sol2 Sib2
    {  654,  778 }, //  3 Do2  Mib2
    { 1038,  980 }, //  4 Lab2 Sol2   baja cromatico
    {  980,  980 }, //  5 Sol2 Sol2   pedal: el V se queda quieto antes de resolver
    {  654,  778 }, //  6 Do2  Mib2   igual que el 3, pero ahora sigue
    {  873, 1038 }, //  7 Fa2  Lab2
    { 1167, 1038 }, //  8 Sib2 Lab2
    {  778,  980 }, //  9 Mib2 Sol2
    { 1038,  980 }, // 10 Lab2 Sol2
    {  734,  873 }, // 11 Re2  Fa2
    {  980,  980 }, // 12 Sol2 Sol2   el pedal otra vez: lo que precede al final
    {  654,  654 }, // 13 Do2
};

// El timbre. El bajo con indice alto y realimentacion, que es lo que le da el
// gruñido de cuerda gorda; las tres de arriba con indice bajo (TL 20 son unos
// 1,4 radianes) y MUL 1, que deja un Rhodes romo en vez de una campana. La de
// arriba lleva MUL 2 para que se despegue del acorde y se oiga la melodia.
// La octava va con indice muy bajo (TL 26) y MUL 1: una senoide casi limpia.
// Si llevara el MUL 2 de la melodia doblaria tambien sus armonicos y sonaria a
// campana en vez de a octava.
static const u8 jazzModTL[JAZZ_VOICES]  = { 16, 20, 22, 20, 26 };
static const u8 jazzModMul[JAZZ_VOICES] = {  1,  1,  1,  2,  1 };
static const u8 jazzFb[JAZZ_VOICES]     = {  5,  2,  2,  0,  0 };

// El golpe y el reposo de cada voz, en TL (menos es mas fuerte, 0,75 dB cada
// uno). El bajo pega mas fuerte porque a 65 Hz el altavoz de un televisor no
// radia el fundamental y lo que llega son los armonicos.
// La octava pega un paso por debajo de la melodia y descansa dos por debajo:
// tiene que DOBLAR, no llevar la voz. Y cae mas rapido, que es lo que deja el
// brillo en el ataque y no en la cola.
static const u8 jazzHit[JAZZ_VOICES] = {  2,  8, 10,  6,  8 };
static const u8 jazzRest[JAZZ_VOICES] = { 20, 28, 30, 26, 32 };
static const u8 jazzFall[JAZZ_VOICES] = {  2,  2,  2,  3,  2 }; // frames por paso de TL
#define JAZZ_FALL_LAST 4   // el ultimo acorde cae mas despacio: tiene que colgar

static u8 jazzStep;    // el acorde que viene, 0..JAZZ_STEPS
static u16 jazzTimer;  // frames que le quedan al acorde puesto
static u8 jazzPulse;   // frames dentro del pulso, 0..JAZZ_BEAT-1
static u8 jazzHalf;    // 0 primer pulso del compas, 1 segundo
static u8 jazzFallTick[JAZZ_VOICES];


// --- LA PERCUSION ----------------------------------------------------------
//
// Peticion del usuario: "con algo de percusion". No hay muestras nuevas: son
// las dos que el juego ya tiene -- KICK y HAT, las mismas del muro y de las
// plantas -- tocadas FLOJO y colgadas de la rejilla del pulso, mas un cepillo
// que sale del canal de ruido del PSG, que durante el jazz no hace nada.
//
// EL PLATO ES EL HI-HAT. El patron es el spang-a-lang, que es lo que hace que
// un ritmo suene a jazz y no a caja de ritmos: un golpe en cada pulso y una
// corchea SWING detras de cada uno. Swing quiere decir a DOS TERCIOS del pulso
// (12 frames de 18) y no a la mitad; a la mitad es una marcha. Las corcheas en
// los dos pulsos y no solo en el segundo es lo que lo hace upbeat: cuatro
// golpes por compas sin huecos.
//
// EL BOMBO SOLO EN EL CAMBIO DE ACORDE, uno por acorde y flojo. En los cuatro
// pulsos seria un four-on-the-floor, que es justo lo que el jazz no es.
//
// EL CEPILLO va a CONTRATIEMPO, en el segundo pulso: un soplo de ruido que se
// apaga en medio segundo. Es lo unico que puede hacer de caja aqui, porque
// muestra de caja no hay -- y un cepillo es lo que lleva una balada de jazz
// de todas formas.
//
// EL REPARTO DE CANALES DEL PCM4. El canal 4 NO se toca: en esta misma
// pantalla el marcador va bajando y suelta un TYPE cada 4 frames, y TYPE vive
// ahi. El bombo se queda el 1, y el plato ALTERNA el 2 y el 3 para que la cola
// de un golpe no corte al siguiente: el hi-hat dura 10 frames y de la corchea
// swing al pulso de al lado solo hay 7.
#define JAZZ_SWING      12  // la corchea, a dos tercios del pulso
#define JAZZ_VOL_KICK    8  // flojo: el bombo acompana, no marca el paso
#define JAZZ_VOL_RIDE    6  // mas flojo todavia: es un plato, no un hi-hat
#define JAZZ_BRUSH_TONE 170 // el divisor del ruido: siseo claro, de cepillo
#define JAZZ_BRUSH_ATT    6 // de donde arranca (0 es fuerte, 15 es nada)
#define JAZZ_BRUSH_FALL   4 // frames por paso: 36 en total, medio segundo

static u8 jazzRideCh;    // el canal del plato: alterna 0 y 1
static u8 jazzBrushAtt;  // la atenuacion del cepillo ahora
static u8 jazzBrushTick;

static u8 jazzBlockOf(u8 v)
{
    return (v < JAZZ_VOICES) ? jazzBlock[v] : 0;
}

// El bombo. Rota las cuatro variantes como el del muro (dos golpes seguidos
// nunca son el mismo sample) pero con su volumen y en su canal.
static void jazzKick(void)
{
    // El bombo del jazz es el LARGO, no el afilado del muro: son dos sonidos
    // distintos y el jazz se queda con el suyo. Rota dos variantes, como antes.
    const u8 v = (u8) (nextKickVariant & 1);

    if (musicOn)
        // Con el nivel de JAZZ_VOL_KICK ya cocido: el driver XGM no tiene
        // volumen por canal.
        XGM_startPlayPCM((u8) (XGM_SMP_JKICK0 + v), XGM_SFX_PRIORITY, SOUND_PCM_CH2);
    else
    {
        SND_PCM4_setVolume(SOUND_PCM_CH1, SFX_VOL_MAX); // el nivel ya va en la muestra
        SND_PCM4_startPlay(jazzKicks[v], jazzKickLens[v], SOUND_PCM_CH1, FALSE);
    }
    sfxShotLeft[SFX_ID_KICK] = SFX_SHOT_FRAMES(kickLens[nextKickVariant]);
    nextKickVariant = (u8) ((nextKickVariant + 1) & 3);
}

// El plato. Alterna canal para que las colas se solapen, que es lo que hace
// que una rejilla de hi-hats suene a plato y no a metronomo.
static void jazzRide(void)
{
    const SoundPCMChannel ch = (SoundPCMChannel) (jazzRideCh ? SOUND_PCM_CH3
                                                             : SOUND_PCM_CH2);

    if (musicOn)
        XGM_startPlayPCM((u8) (XGM_SMP_JRIDE0 + nextHihatVariant), XGM_SFX_PRIORITY,
                         (SoundPCMChannel) (jazzRideCh ? SOUND_PCM_CH3 : SOUND_PCM_CH4));
    else
    {
        SND_PCM4_setVolume((u8) ch, JAZZ_VOL_RIDE);
        SND_PCM4_startPlay(hihats[nextHihatVariant], hihatLens[nextHihatVariant],
                           ch, FALSE);
    }
    sfxShotLeft[SFX_ID_HAT] = SFX_SHOT_FRAMES(hihatLens[nextHihatVariant]);
    nextHihatVariant = (u8) ((nextHihatVariant + 1) & 3);
    jazzRideCh ^= 1;
}

// El cepillo.
static void jazzBrushHit(void)
{
    // El ruido se reengancha al divisor del canal 2: el visor de mapa usa este
    // mismo canal para su estatica y pudo dejarlo en su reloj fijo.
    PSG_setNoise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_TONE3);
    PSG_setTone(2, JAZZ_BRUSH_TONE);
    jazzBrushAtt = JAZZ_BRUSH_ATT;
    jazzBrushTick = 0;
    PSG_setEnvelope(3, jazzBrushAtt);
}

// Su desvanecido, un paso de atenuacion cada JAZZ_BRUSH_FALL frames. Sigue
// corriendo durante la cola del ultimo acorde: el cepillo acaba de apagarse
// aunque ya no entre ninguno mas.
static void jazzBrushFade(void)
{
    if (jazzBrushAtt >= PSG_ENVELOPE_MIN)
        return;

    jazzBrushTick++;
    if (jazzBrushTick < JAZZ_BRUSH_FALL)
        return;
    jazzBrushTick = 0;
    jazzBrushAtt++;
    PSG_setEnvelope(3, jazzBrushAtt);
}

static void jazzBrushOff(void)
{
    PSG_setEnvelope(3, PSG_ENVELOPE_MIN);
    jazzBrushAtt = PSG_ENVELOPE_MIN;
}

// Apaga las cuatro voces y se quita de en medio. Lo llama Sfx_slopStop, que es
// por donde se sale de la pantalla de game over y tambien por donde pasa el
// apagar el sonido con la C.
// Corta las cinco voces EN SECO: el chip mudo y la cache invalidada, para que
// quien las coja despues no las de por abiertas. Lo piden los tres sitios que
// cambian de sonido de golpe -- entrar y salir del jazz, y elegir planeta --
// porque ahi desvanecer no vale: detras se reescribe el modulador de esas
// mismas voces, y cambiarselo a una que esta sonando es un chasquido.
//
// clearGlide pone ademas a cero el reloj de deslizamiento. Lo piden los dos
// que montan un sonido NUEVO encima; apagar y ya no lo necesita.
static void slopCutVoices(bool clearGlide)
{
    u8 v;

    for (v = 0; v < SLOP_VOICES; v++)
    {
        voiceAttNow[v] = SLOP_TL_SILENT;
        voiceAttWritten[v] = SLOP_TL_SILENT;
        voiceToneWritten[v] = 0;
        wrTL[v] = SLOP_TL_SILENT;
        wrTLMask |= (u8) (1 << v);
        if (clearGlide)
            gTimer[v] = 0;
    }
    slopFlush();
}

static void jazzSilence(void)
{
    jazzOn = FALSE;
    jazzBrushOff();
    slopCutVoices(FALSE);
}

// Pone el acorde jazzStep y avanza. El bajo entra con su primera nota.
static void jazzStrike(void)
{
    u8 v;

    for (v = 0; v < JAZZ_VOICES; v++)
    {
        const u16 hz10 = (v == 0) ? jazzBass[jazzStep][0]
                       : (v == JAZZ_OCTAVE_VOICE) ? jazzOctaveHz10(jazzStep)
                       : jazzChord[jazzStep][v - 1];
        const u16 t = slopFnum(hz10, jazzBlock[v]);

        // Sin deslizamiento y sin deriva: un acorde de piano no se desafina
        // solo. gTimer a 0 deja a slopVoiceTone devolviendo voiceTone tal cual.
        voiceTone[v] = t;
        voiceCur[v] = t;
        gTimer[v] = 0;
        voiceDriftPhase[v] = 0;
        voiceDriftRate[v] = 0;
        slopDriftStep[v] = 0;

        voiceAttNow[v] = jazzHit[v];
        jazzFallTick[v] = 0;
    }
    jazzTimer = (u16) ((jazzStep == (JAZZ_STEPS - 1)) ? JAZZ_RING : JAZZ_CHORD);
    jazzPulse = 0;
    jazzHalf = 0;
    jazzStep++;

    // El golpe del primer pulso, en el MISMO frame que el acorde. Tambien en
    // el ultimo: ahi es el golpe final, y detras ya no entra nada mas.
    jazzRide();
    jazzKick();
}

// El bajo pisa la segunda nota del compas, y vuelve a pegar. Las tres de
// arriba no: siguen cayendo desde el golpe del acorde, que es lo que hace que
// el bajo ande por delante en vez de sonar todo a la vez.
static void jazzWalk(void)
{
    const u16 t = slopFnum(jazzBass[jazzStep - 1][1], jazzBlock[0]);

    voiceTone[0] = t;
    voiceCur[0] = t;
    gTimer[0] = 0;
    voiceAttNow[0] = jazzHit[0];
    jazzFallTick[0] = 0;
}

// Un frame del corte. Lo llama Sfx_update y sustituye al ambiente entero
// mientras dura: las dos cosas escriben los mismos canales del chip.
static void jazzTick(void)
{
    const bool last = (jazzStep >= JAZZ_STEPS);
    u8 v;

    if (!enabled)
    {
        jazzSilence();
        return;
    }

    if (jazzTimer == 0)
    {
        if (last) // el ultimo acorde ha acabado de colgar
        {
            jazzSilence();
            return;
        }
        jazzStrike(); // el primer pulso del compas lo da el, con el acorde
    }
    else
    {
        // EL RELOJ DEL PULSO, y lo lleva uno solo: de el cuelgan el bajo que
        // anda, el plato y el cepillo, asi que caen en el MISMO frame en vez
        // de uno detras de otro. Con JAZZ_CHORD a dos pulsos, el segundo cae
        // a mitad de compas y el primero del compas siguiente lo da jazzStrike.
        jazzPulse++;
        if (jazzPulse >= JAZZ_BEAT)
        {
            jazzPulse = 0;
            jazzHalf = 1;
            if (!last)
            {
                jazzWalk();     // el bajo pisa su segunda nota
                jazzRide();
                jazzBrushHit(); // el cepillo, a contratiempo
            }
        }
        else if (!last && (jazzPulse == JAZZ_SWING))
        {
            jazzRide();         // la corchea swing, en los DOS pulsos
        }
    }
    jazzTimer--;
    jazzBrushFade();

    for (v = 0; v < JAZZ_VOICES; v++)
    {
        // A donde cae. El ultimo acorde cae hasta lo inaudible y por eso se
        // apaga el corte entero cuando se le acaba el tiempo.
        const u8 rest = last ? SLOP_TL_QUIET : jazzRest[v];
        const u8 every = last ? JAZZ_FALL_LAST : jazzFall[v];

        if (voiceAttNow[v] < rest)
        {
            jazzFallTick[v]++;
            if (jazzFallTick[v] >= every)
            {
                jazzFallTick[v] = 0;
                voiceAttNow[v]++;
            }
        }

        if (voiceToneWritten[v] != voiceTone[v])
        {
            voiceToneWritten[v] = voiceTone[v];
            wrTone[v] = voiceTone[v];
            wrToneMask |= (u8) (1 << v);
        }
        if (voiceAttWritten[v] != voiceAttNow[v])
        {
            voiceAttWritten[v] = voiceAttNow[v];
            wrTL[v] = voiceAttNow[v];
            wrTLMask |= (u8) (1 << v);
        }
    }
    slopFlush();
}

// Arranca el corte. Lo llama gameOverBegin en vez del Sfx_slopStop que tenia.
void Sfx_jazzGameOver(void)
{
    u8 v;

    // Primero el ambiente fuera, y en seco: detras se reescribe el modulador
    // de estas mismas voces, y cambiarselo sonando es un chasquido. Es el
    // mismo corte y por la misma razon. La cancion se PAUSA, no se para: al
    // volver a la fase sigue por donde iba (ver musicPause).
    Sfx_musicPause();
    slopReleasing = FALSE;
    psgNoiseStop(); // el canal de ruido tambien: el jazz lo va a querer
    slopCutVoices(TRUE);

    if (!enabled)
        return;

    // jazzOn ANTES del timbre: slopVoiceTimbre no lo lee, pero slopFlush si, y
    // el primer golpe tiene que salir ya con el block del jazz.
    jazzOn = TRUE;
    jazzStep = 0;
    jazzTimer = 0; // el primer acorde entra en el primer frame
    jazzPulse = 0;
    jazzHalf = 0;
    jazzRideCh = 0;
    jazzBrushAtt = PSG_ENVELOPE_MIN; // nada sonando hasta el primer contratiempo
    jazzBrushTick = 0;

    // El LFO global fuera: el vibrato es del ambiente, aqui estorba.
    Z80_requestBus(TRUE);
    YM2612_writeReg(0, 0x22, 0x00);
    YM2612_write(0, 0x2A);
    Z80_releaseBus();

    for (v = 0; v < JAZZ_VOICES; v++)
        slopVoiceTimbre(v, jazzModTL[v], jazzModMul[v], 0, jazzFb[v], 0);
}

// La melodica no toca el PSG: el siseo es solo de la sala de maquinas, y el
// canal de ruido se lo queda entero el visor de mapa.

// Calla el canal de ruido del PSG. El visor de mapa lo usa para su estatica y
// el cepillo del jazz para el suyo, y nunca coinciden en pantalla.
static void psgNoiseStop(void)
{
    PSG_setEnvelope(3, PSG_ENVELOPE_MIN);
}


// --- LA ESCENA -------------------------------------------------------------

// De que planeta es la cancion. Se apunta SIEMPRE, aunque el sonido este
// apagado ahora: la partida lo lee al cambiar de escena y puede encenderse
// despues. Va por el planeta y no por la fila sorteada, ver planetMusic[].
void Sfx_setPlanet(u8 planet)
{
    musicTrack = (u8) (planetTrack[planet % MENU_PLANETS] % MUSIC_TRACKS);
}

// La musica entra y sale con la escena. No convive con el PCM4, asi que cada
// cambio reinstala el driver que toca -- ver musicStart/musicStop.
void Sfx_musicScene(bool menu)
{
    if (menu)
        musicPause();   // el menu calla, pero la cancion se queda donde esta
    else
        musicStart();   // volver a entrar a la partida la relanza
}


// Un frame. Solo queda la cuenta atras de las muestras y el corte del game
// over: la musica la lleva el Z80 por su cuenta y no pide nada por frame.
void Sfx_update(void)
{
    u8 v;

    // Antes del corte de abajo: una muestra puede estar sonando con el jazz
    // puesto, y el visor tiene que verla igual.
    for (v = 0; v < SFX_ID_SHOTS; v++)
        if (sfxShotLeft[v] > 0)
            sfxShotLeft[v]--;

    if (splashLeft)
        splashTick();

    if (jazzOn)
        jazzTick();
}


// --- EL INVENTARIO, PARA PODER HABLAR DE ESTO ------------------------------
//
// Peticion del usuario: "quiero que identifiques cada sonido en pantalla,
// escribe su codename para que podamos hablar de ello... hay algunos que
// quiero eliminar". Estos son TODOS los sonidos que el juego puede emitir, con
// el nombre por el que se les llama de aqui en adelante. El orden es el de los
// identificadores de sfx.h, y no es arbitrario: primero las muestras del
// driver PCM4, luego las voces FM de cada modalidad y al final el PSG.
//
// Que esten aqui y no en un comentario suelto es a proposito: el visor de
// depuracion los lee de esta misma tabla, asi que lo que se ve en pantalla no
// puede desincronizarse de lo que dice el codigo. KILL se quedo en su sitio
// aunque ya no sea una muestra sino PSG: el orden es el de los identificadores
// de sfx.h y moverlo seria renumerarlos todos.
static const char *const sfxName[SFX_ID_COUNT] = {
    "KICK",   // 0  bombo 909 al chocar con un muro, 4 variantes que rotan
    "HAT",    // 1  hi-hat cerrado al coger una planta, 4 variantes
    "KILL",   // 2  matar un enemigo -- el ruido del PSG, pitch sorteado
    "LETTER", // 3  coger una letra -- PLACEHOLDER, una sola muestra
    "TYPE",   // 4  clic del typewriter, lo mas flojo del juego

    "JAZZ",   // 5  la cadencia del game over, las cuatro voces como una sola
    "BRUSH",  // 6  el cepillo del jazz: el ruido del PSG a contratiempo

    "MUSIC",  // 7  partida: la cancion del planeta, por el driver XGM
};

// Una linea de que es cada uno, para el visor. Corta a proposito: la pantalla
// son 40 columnas y el nombre ya se come las primeras.
static const char *const sfxWhat[SFX_ID_COUNT] = {
    "muro", "planta", "enemigo", "letra", "texto",
    "game over", "cepillo",
    "", // MUSIC: no es fija, la pone Sfx_debugWhat con el nombre de la cancion
};

const char *Sfx_debugName(u8 id)
{
    return (id < SFX_ID_COUNT) ? sfxName[id] : "?";
}

const char *Sfx_debugWhat(u8 id)
{
    // La fila de la musica dice QUE cancion suena, que es lo unico que la
    // distingue de un planeta a otro.
    if (id == SFX_ID_MUSIC)
        return planetMusicName[musicTrack];
    return (id < SFX_ID_COUNT) ? sfxWhat[id] : "";
}

// De TL (0 fuerte, SLOP_TL_QUIET callado) a los 0..15 que pinta el visor.
static u8 sfxBarFromTL(u8 att)
{
    if (att >= SLOP_TL_QUIET)
        return 0;
    return (u8) (((SLOP_TL_QUIET - att) * 15u) / SLOP_TL_QUIET);
}

u8 Sfx_debugLevel(u8 id)
{
    if (!enabled)
        return 0;

    if (id < SFX_ID_SHOTS)
        return sfxShotLeft[id] ? 15 : 0;

    // El jazz son cuatro voces pero un solo sonido: la barra es la mas fuerte
    // de las cuatro, que es la que el oido sigue.
    if (id == SFX_ID_JAZZ)
    {
        u8 v, best = SLOP_TL_SILENT;

        if (!jazzOn)
            return 0;
        for (v = 0; v < JAZZ_VOICES; v++)
            if (voiceAttNow[v] < best)
                best = voiceAttNow[v];
        return sfxBarFromTL(best);
    }

    if (id == SFX_ID_BRUSH)
        return (jazzOn && (jazzBrushAtt < PSG_ENVELOPE_MIN))
                 ? (u8) (PSG_ENVELOPE_MIN - jazzBrushAtt) : 0;

    // La cancion la lleva el Z80: aqui no hay nivel que leer, solo si suena.
    if (id == SFX_ID_MUSIC)
        return musicOn ? 15 : 0;

    return 0;
}

bool Sfx_debugExists(u8 id)
{
    if (id < SFX_ID_SHOTS)
        return TRUE;
    // El jazz solo existe en la pantalla de game over, que es justo donde el
    // visor no se abre -- asi que en el visor sale siempre como que no esta.
    // Esta en la lista para poder hablar de el, no para verlo sonar.
    if (id == SFX_ID_JAZZ || id == SFX_ID_BRUSH)
        return jazzOn;
    if (id == SFX_ID_MUSIC)
        return musicOn;
    return FALSE;
}

void Sfx_debugSolo(u8 id)
{
    sfxSolo = id;
}
