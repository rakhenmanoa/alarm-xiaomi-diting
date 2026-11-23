#!/usr/bin/env python3
"""Convertit une image brute RAW10 compactée MIPI (CSI2P, 4 pixels dans 5 octets, comme la
livre `cam -s role=raw`) en entrée du BPS : 10 bits dans des mots de 16 bits, LARGEUR * 2
octets par ligne.

Usage : raw10-to-bps.py image.raw LARGEUR HAUTEUR sortie.bin [LARGEUR_SOURCE HAUTEUR_SOURCE]
Sans taille source, l'image fait LARGEUR x HAUTEUR. Avec, on garde le centre de l'image source
(décalages pairs : l'ordre Bayer ne change pas). Le pas de ligne est déduit de la taille du
fichier."""
import sys

src, w, h, dst = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
sw, sh = (int(sys.argv[5]), int(sys.argv[6])) if len(sys.argv) > 6 else (w, h)
d = open(src, 'rb').read()
stride = len(d) // sh
if stride < sw * 5 // 4 or w > sw or h > sh:
    sys.exit(f'{len(d)} octets : ne correspond pas à {sw}x{sh} en RAW10 compacté')
x0 = (sw - w) // 2 // 4 * 4		# multiple de 4 : début d'un groupe de 5 octets
y0 = (sh - h) // 2 // 2 * 2
out = bytearray(w * h * 2)
total = 0
for y in range(h):
    start = (y0 + y) * stride + x0 * 5 // 4
    line = d[start:start + w * 5 // 4]
    o = y * w * 2
    for i in range(0, w * 5 // 4, 5):
        b = line[i:i + 5]
        lo = b[4]
        for k in range(4):
            v = (b[k] << 2) | ((lo >> (2 * k)) & 3)
            out[o] = v & 0xff
            out[o + 1] = v >> 8
            o += 2
            total += v
open(dst, 'wb').write(out)
print(f'{w}x{h} pris en ({x0},{y0}) dans {sw}x{sh}, pas {stride} octets, '
      f'moyenne {total / (w * h):.1f} sur 1023 -> {dst}')
