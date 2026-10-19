#!/usr/bin/env python3
"""Cierra un .xgm en bucle: cambia su END ($7F) por un LOOP ($7E) al principio.

Las canciones que exporta el generador terminan y se callan; en una partida eso
deja el planeta mudo. El driver XGM repite solo si el dato trae el comando de
bucle, asi que se lo metemos aqui y no en tiempo de ejecucion (el bucle del
dato no tiene hueco ni cuesta ciclos). Es lo mismo que ya traen chop.xgm y
AVENTURA_1374.xgm, que se generaron con bucle.

Uso: tools/xgm_loop.py entrada.xgm salida.xgm
"""
import sys

def main(src, dst):
    d = bytearray(open(src, 'rb').read())
    if d[:4] != b'XGM ':
        sys.exit("%s no es un .xgm (falta la marca 'XGM ')" % src)
    flags = d[0x103]
    if flags & 0x02:
        sys.exit("%s lleva etiquetas GD3 detras del dato; este script no las mueve" % src)
    if flags & 0x04:
        sys.exit("%s es multipista; este script no lo contempla" % src)

    slen = (d[0x100] | (d[0x101] << 8)) * 256      # bloque de muestras
    mpos = 0x104 + slen                            # campo MLEN
    mlen = int.from_bytes(d[mpos:mpos + 4], 'little')
    end = mpos + 4 + mlen
    if end != len(d):
        sys.exit("%s: el dato de musica no llega hasta el final del fichero" % src)

    if d[end - 4] == 0x7E:
        print("%s ya cierra en bucle; copiado tal cual" % src)
        open(dst, 'wb').write(d)
        return
    if d[end - 1] != 0x7F:
        sys.exit("%s no termina en END ($7F); no se toca" % src)

    # $7F (1 byte) -> $7E ddddd (4 bytes), con el destino en el offset 0 del
    # bloque de musica, que es donde vuelven las dos que si traian bucle.
    d[end - 1:end] = b'\x7E\x00\x00\x00'
    mlen += 3
    d[mpos:mpos + 4] = mlen.to_bytes(4, 'little')
    open(dst, 'wb').write(d)
    print("%s -> %s  (END cambiado por LOOP, MLEN %d)" % (src, dst, mlen))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
