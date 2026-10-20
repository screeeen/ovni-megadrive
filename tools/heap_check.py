#!/usr/bin/env python3
"""Avisa si el ROM recien enlazado no va a poder arrancar por falta de heap.

SGDK pide memoria ANTES de que corra una sola linea del juego, y si no la hay
se muere en VDP_resetScreen con "not enough memory to reset VDP". Como el heap
es lo que sobra detras de la BSS, cualquier array estatico nuevo lo recorta: el
fallo no da la cara al compilar ni al enlazar, solo al arrancar.
"""
import os, subprocess, sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(RAIZ, "out/release/rom.out")

STACK = 0x0A00                  # memory_base.h
MEM_HIGH = 0xE1000000 - STACK

# Lo que SGDK reserva en el arranque, en este orden (sys.c: MEM_init, DMA_init,
# VDP_init). MEM_alloc redondea a par y se queda 2 bytes de cabecera.
def alloc(n):
    return (n + 2 + 1) & 0xFFFE

COLA_DMA = alloc(80 * 16)       # DMA_QUEUE_SIZE_DEFAULT x sizeof(DMAOpInfo)
BUFFER_DMA = alloc(8192)        # DMA_BUFFER_SIZE_NTSC
FUENTE = alloc(96 * 32)         # font_default, comprimida: se descomprime en heap
ARRANQUE = COLA_DMA + BUFFER_DMA + FUENTE


def bend():
    for herr in ("m68k-elf-nm", os.path.join(os.environ.get("GDK", ""), "bin/m68k-elf-nm")):
        try:
            sal = subprocess.run([herr, OUT], capture_output=True, text=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            continue
        for linea in sal.splitlines():
            p = linea.split()
            if len(p) == 3 and p[2] == "_bend":
                return int(p[0], 16)
    sys.exit("heap_check: no encuentro _bend en %s" % OUT)


def main():
    h = (bend() + 1) & ~1
    heap = MEM_HIGH - (h + 2)
    margen = heap - ARRANQUE

    print("heap %d B, el arranque pide %d B, margen %d B" % (heap, ARRANQUE, margen))
    if margen < 0:
        sys.exit("heap_check: EL ROM NO ARRANCA, faltan %d bytes. Quita BSS." % -margen)
    if margen < 512:
        print("heap_check: aviso, quedan menos de 512 B de margen")


main()
