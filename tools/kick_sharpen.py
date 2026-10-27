#!/usr/bin/env python3
"""Afila el bombo y saca las copias para el driver XGM.

De res/sound/kick_src*.wav salen kick*.wav, kick_xgm*.wav y las dos del jazz
del game over (kick_jazz.wav y hihat_jazz.wav, esta de hihat0.wav).

Peticion del usuario ("afilalo"). El bombo original dura 300 ms y cae lentisimo
-- a los 150 ms todavia esta al 43% del pico -- asi que el cuerpo tapa el golpe
y el impacto no se pega al frame. Esto NO cambia el timbre: el barrido de tono
(200 Hz a 50) y la forma de onda son los mismos, lo unico que se toca es la
envolvente, que pasa a caer en CAIDA_MS y el rabo se corta en LARGO_MS.

Los de _xgm son los mismos a XGM_NIVEL: el driver XGM no tiene volumen por
canal, asi que el nivel va cocido en el fichero (era lo que ya pasaba).

Las del jazz del game over son otra cosa y por eso van aparte: el jazz quiere
el bombo LARGO, el de 300 ms, que es el que tenia cuando sonaba por PCM4 -- si
se le da el afilado suena a otra cosa (y eso fue exactamente lo que paso). Solo
se les baja el nivel al que tenian por canal (JAZZ_VOL_KICK 8 y JAZZ_VOL_RIDE 6
de 15), porque el driver XGM no tiene volumen por canal.
"""
import os, struct, sys, wave

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUENTE = os.path.join(RAIZ, "res/sound/kick_src%d.wav")
SAL_PCM = os.path.join(RAIZ, "res/sound/kick%d.wav")
SAL_XGM = os.path.join(RAIZ, "res/sound/kick_xgm%d.wav")
VARIANTES = 4

CAIDA_MS = 28.0   # constante de la caida: a los 3x esto queda el 5%
LARGO_MS = 110.0  # lo que dura el fichero ya cortado
FUNDIDO_MS = 6.0  # el final, para que el corte no haga clic
XGM_NIVEL = 0.754 # el mismo que tenian los _xgm
JAZZ_KICK_NIVEL = 8.0 / 15.0
JAZZ_RIDE_NIVEL = 6.0 / 15.0
JAZZ_KICKS = 2    # los que rota el jazz
JAZZ_RIDES = 4


def leer(ruta):
    w = wave.open(ruta)
    fr, nf, sw, ch = w.getframerate(), w.getnframes(), w.getsampwidth(), w.getnchannels()
    if (sw, ch) != (2, 1):
        sys.exit("%s no es mono de 16 bits" % ruta)
    return fr, list(struct.unpack("<%dh" % nf, w.readframes(nf)))


def escribir(ruta, fr, v):
    w = wave.open(ruta, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(fr)
    w.writeframes(struct.pack("<%dh" % len(v), *v))
    w.close()


def afilar(fr, v, nivel):
    import math
    largo = min(len(v), int(fr * LARGO_MS / 1000.0))
    tau = fr * CAIDA_MS / 1000.0
    fundido = int(fr * FUNDIDO_MS / 1000.0)
    salida = []

    for i in range(largo):
        g = math.exp(-i / tau)
        if i >= largo - fundido:
            g *= (largo - i) / float(fundido)
        x = int(v[i] * g * nivel)
        salida.append(max(-32768, min(32767, x)))

    return salida


def escalar(ruta_in, ruta_out, nivel):
    """Tal cual pero mas flojo: para las copias del jazz."""
    fr, v = leer(ruta_in)
    escribir(ruta_out, fr, [max(-32768, min(32767, int(x * nivel))) for x in v])


def main():
    for n in range(VARIANTES):
        fr, v = leer(FUENTE % n)
        corto = afilar(fr, v, 1.0)
        escribir(SAL_PCM % n, fr, corto)
        escribir(SAL_XGM % n, fr, afilar(fr, v, XGM_NIVEL))
        if n == 0:
            win = int(0.01 * fr)
            pico = max(abs(x) for x in corto)
            niveles = [int(100 * max(abs(x) for x in corto[i:i + win]) / pico)
                       for i in range(0, len(corto), win)]
            print("envolvente nueva por 10 ms: %s" % " ".join(str(x) for x in niveles))
    # El jazz: el bombo entero (sin afilar) y los cuatro platos, solo bajados
    # de nivel. Dos bombos y cuatro platos, que es lo que rota el jazz.
    for n in range(JAZZ_KICKS):
        escalar(FUENTE % (n * 2), os.path.join(RAIZ, "res/sound/kick_jazz%d.wav" % n),
                JAZZ_KICK_NIVEL)
    for n in range(JAZZ_RIDES):
        escalar(os.path.join(RAIZ, "res/sound/hihat%d.wav" % n),
                os.path.join(RAIZ, "res/sound/hihat_jazz%d.wav" % n), JAZZ_RIDE_NIVEL)
    print("bombo: %d variantes de %.0f ms (antes 300), caida %.0f ms"
          % (VARIANTES, LARGO_MS, CAIDA_MS))
    print("jazz: %d bombos LARGOS al %.0f%% y %d platos al %.0f%%"
          % (JAZZ_KICKS, 100 * JAZZ_KICK_NIVEL, JAZZ_RIDES, 100 * JAZZ_RIDE_NIVEL))


main()
