#!/usr/bin/env python3
"""Hace las variantes de res/sound/enemykill0..5.wav, el splash de matar enemigo.

Antes era un zap de barrido (cuadrado 970->70 Hz) y una sola muestra: se oia a
tono y siempre igual. Ahora el ruido manda -- el "splash" es el brillo cayendo,
un paso banda barriendo de SPLASH_HI a SPLASH_LO, con un chasquido ancho al
golpear y un golpe corto debajo para que aterrice -- y van SEIS, cada una a su
pitch, porque ni el PCM4 ni el XGM saben cambiar la velocidad de reproduccion:
la variacion hay que cocerla en el fichero. sfx.c las rota.

PITCHES las da en el orden en que suenan, y ese orden es a proposito desordenado
(0,86 detras de 1,18): puestas de grave a agudo el ciclo de seis se oye como una
escala. Cada una lleva ademas su propia semilla de ruido, asi que no es el mismo
golpe transportado sino otro golpe.

Mandos: SPLASH_MS el largo a pitch 1, CUERPO_MS el golpe de abajo, SPLASH_HI/LO
el barrido del filtro, GRANO lo metalico, SATURA lo aplastado.
"""
import math
import wave

import numpy as np

SR = 16000
PITCHES = (1.00, 1.26, 0.78, 1.12, 0.88, 0.70)
SPLASH_MS = 300      # el zap duraba 190; un splash necesita cola
CUERPO_MS = 55       # golpe de apoyo (el impacto en si)
SPLASH_HI = 4200.0   # centro del filtro al golpear a pitch 1. Mas arriba no:
                     # la variante aguda se come la campana del filtro contra
                     # Nyquist (8000 a 16 kHz) y deja de subir de brillo.
SPLASH_LO = 1100.0   # ...y al apagarse
Q = 1.10
CHASQUIDO = 0.28     # ruido sin filtrar al golpear; de banda entera, o
                     # sea que tapa el pitch, por eso no mas alto
GRANO = 0.45         # 0 = ruido liso, 1 = muy metalico/granulado
CUERPO_NIVEL = 0.30
SATURA = 2.4         # a igual pico, el ruido se oye mas flojo que un tono
PICO = 0.92
RMS_OBJ = 0.19       # medido en los primeros RMS_VENTANA_MS, no en todo el
RMS_VENTANA_MS = 150 # fichero: las graves son mas largas y saldrian flojas


def splash(pitch, semilla):
    rng = np.random.default_rng(semilla)

    # Mas agudo suena a plato mas pequeno: tambien mas corto, no solo mas alto.
    largo = pitch ** -0.5
    n = int(SR * SPLASH_MS / 1000 * largo)
    t = np.arange(n) / SR

    # Ruido con grano: blanco modulado por ruido lento, que es lo que da el
    # chisporroteo de plato en vez de un siseo plano.
    blanco = rng.uniform(-1.0, 1.0, n)
    lento = rng.uniform(0.0, 1.0, (n // 24) + 2)
    lento = np.interp(np.arange(n) / 24.0, np.arange(len(lento)), lento)
    fuente = blanco * (1.0 - GRANO + GRANO * lento * 2.0)

    # Paso banda (biquad RBJ) con el centro barriendo hacia abajo: brillante al
    # golpear, oscuro en la cola. Biquad y no SVF porque el SVF de dos polos se
    # desestabiliza con el centro tan arriba a 16 kHz de muestreo.
    corte = np.clip(SPLASH_HI * pitch * (SPLASH_LO / SPLASH_HI) ** (t / t[-1]),
                    40.0, SR * 0.47)
    w0 = 2.0 * np.pi * corte / SR
    alpha = np.sin(w0) / (2.0 * Q)
    a0 = 1.0 + alpha
    b0 = alpha / a0
    b2 = -alpha / a0
    a1 = (-2.0 * np.cos(w0)) / a0
    a2 = (1.0 - alpha) / a0

    salida = np.empty(n)
    x1 = x2 = y1 = y2 = 0.0
    for i in range(n):
        x0 = fuente[i]
        y0 = b0[i] * x0 + b2[i] * x2 - a1[i] * y1 - a2[i] * y2
        x2, x1 = x1, x0
        y2, y1 = y1, y0
        salida[i] = y0

    # Chasquido del impacto: ruido crudo, ancho, solo los primeros milisegundos.
    salida += fuente * CHASQUIDO * np.exp(-t / (0.006 * largo))

    # Envolvente en dos tramos: el golpe se cae rapido y luego queda la cola.
    env = 0.55 * np.exp(-t / (0.018 * largo)) + 0.45 * np.exp(-t / (0.095 * largo))
    env *= np.clip(t / 0.0015, 0.0, 1.0)           # ataque, sin click
    env *= np.clip((t[-1] - t) / 0.030, 0.0, 1.0)  # y muere en cero
    salida *= env

    # Golpe de cuerpo: seno 190 -> 70 Hz, corto, solo para aterrizar. Sube con
    # el pitch pero menos que el ruido, o en las agudas se queda sin peso.
    cuerpo_pitch = pitch ** 0.6
    nc = int(SR * CUERPO_MS / 1000 * largo)
    tc = np.arange(nc) / SR
    fc = 190.0 * cuerpo_pitch * (70.0 / 190.0) ** (tc / tc[-1])
    fase = 2.0 * math.pi * np.cumsum(fc) / SR
    salida[:nc] += np.sin(fase) * np.exp(-tc / (0.020 * largo)) * CUERPO_NIVEL

    # A igual pico, el ruido rinde la mitad de rms que el tono del zap viejo,
    # asi que se oiria mas flojo: la saturacion suave le sube el cuerpo sin
    # pasarse de pico, y de paso lo hace mas denso para los 8 bits del sample.
    salida /= np.max(np.abs(salida))
    salida = np.tanh(SATURA * salida) / np.tanh(SATURA)
    salida *= PICO / np.max(np.abs(salida))

    # Y al mismo nivel las seis: a igual pico el rms se iba 4 dB entre unas y
    # otras, que se oye como que hay muertes que pegan mas fuerte y no como
    # otro pitch. Gana el pico si las dos cosas no caben.
    ventana = salida[:int(SR * RMS_VENTANA_MS / 1000)]
    k = min(RMS_OBJ / np.sqrt((ventana ** 2).mean()), PICO / np.max(np.abs(salida)))
    return salida * k


for v, pitch in enumerate(PITCHES):
    d = splash(pitch, 20261027 + v * 101)
    pcm = np.clip(d * 32767.0, -32768, 32767).astype(np.int16)
    ruta = "res/sound/enemykill%d.wav" % v
    with wave.open(ruta, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print("%s  pitch %.2f  %4d muestras  %3.0f ms" % (ruta, pitch, len(d), 1000 * len(d) / SR))
