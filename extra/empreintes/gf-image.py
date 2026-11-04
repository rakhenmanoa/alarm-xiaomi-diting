#!/usr/bin/python3 -I
"""Images brutes d'une capture d'usine Goodix (gfdit, commande 1003/21).

  gf-image.py usine-1003-21-N.bin [préfixe]

Le fichier est celui qu'écrit « GF_DUMP=préfixe qseecom-cli ... 1003 21 921640 » :
en-tête de 28 octets, lignes et colonnes en [28] et [32], puis des trames de
lignes × colonnes pixels de 16 bits (petit-boutiste) à la suite. Chaque trame
non vide est écrite en PGM 8 bits (contraste étiré) : préfixe-0.pgm, ...
Affiche aussi min / max / moyenne de chaque trame.
"""
import struct
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    data = open(sys.argv[1], 'rb').read()
    prefix = sys.argv[2] if len(sys.argv) > 2 else 'trame'
    rows, cols = struct.unpack_from('<II', data, 28)
    n = rows * cols
    print(f'{rows} lignes × {cols} colonnes')
    off, k = 36, 0
    while off + 2 * n <= len(data):
        px = struct.unpack_from(f'<{n}H', data, off)
        if not any(px):
            break
        lo, hi = min(px), max(px)
        print(f'trame {k} : min {lo}, max {hi}, moyenne {sum(px) / n:.1f}')
        span = max(hi - lo, 1)
        img = bytes((p - lo) * 255 // span for p in px)
        with open(f'{prefix}-{k}.pgm', 'wb') as f:
            f.write(b'P5 %d %d 255\n' % (cols, rows) + img)
        off += 2 * n
        k += 1


if __name__ == '__main__':
    main()
