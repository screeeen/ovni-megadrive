#!/usr/bin/env python3
"""Cierra un .vgm en bucle: le pone el punto de bucle al principio del dato.

El hermano de xgm_loop.py, que hace lo mismo con el formato del driver viejo.
Hace falta porque las canciones de res/music nacieron como .xgm (driver XGM1) y
el XGM2 solo come .vgm, asi que hubo que volver atras con xgmtool -- y esa
vuelta NO trae el bucle: deja los dos campos de bucle de la cabecera VGM a
cero, o sea "suena una vez y se calla". En una partida eso deja el planeta
mudo a los dos minutos.

El bucle que se pone es el del dato original (ver xgm_loop.py): vuelta al
PRINCIPIO de la cancion, no a un estribillo. Son dos campos de la cabecera, no
hay que tocar el dato.

La receta entera, por si hay que repetirla:
    xgmtool res/music/X.xgm res/music/X.vgm     # XGM1 -> VGM (pierde el bucle)
    tools/vgm_loop.py res/music/X.vgm           # y se lo devolvemos

Uso: tools/vgm_loop.py fichero.vgm [fichero.vgm ...]   (los reescribe en sitio)
"""
import struct, sys

def cierra(ruta):
    d = bytearray(open(ruta, "rb").read())
    if d[:4] != b"Vgm ":
        sys.exit("%s no es un .vgm (falta la marca 'Vgm ')" % ruta)

    version = struct.unpack_from("<I", d, 0x08)[0]
    total   = struct.unpack_from("<I", d, 0x18)[0]
    bucle   = struct.unpack_from("<I", d, 0x1C)[0]

    # Donde empieza el dato: el campo existe desde la 1.50, y antes era fijo.
    inicio = 0x40
    if version >= 0x150:
        rel = struct.unpack_from("<I", d, 0x34)[0]
        if rel:
            inicio = 0x34 + rel

    if bucle:
        print("%s ya cierra en bucle (offset %d); no se toca" % (ruta, bucle))
        return

    struct.pack_into("<I", d, 0x1C, inicio - 0x1C)  # relativo a su propio campo
    struct.pack_into("<I", d, 0x20, total)          # y repite la cancion entera
    open(ruta, "wb").write(d)
    print("%s: bucle al offset %d (%d muestras)" % (ruta, inicio, total))

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for r in sys.argv[1:]:
        cierra(r)
