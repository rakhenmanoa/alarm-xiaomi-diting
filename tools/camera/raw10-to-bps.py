#!/usr/bin/env python3
"""Convertit une image brute RAW10 compactée MIPI (CSI2P, 4 pixels dans 5 octets, comme la
livre `cam -s role=raw`) en entrée du BPS : 10 bits dans des mots de 16 bits, LARGEUR * 2
octets par ligne. Le pas de ligne du fichier est déduit de sa taille.

Usage : raw10-to-bps.py image.raw LARGEUR HAUTEUR sortie.bin"""
import sys

src, w, h, dst = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
d = open(src, 'rb').read()
stride = len(d) // h
if stride < w * 5 // 4:
    sys.exit(f'{len(d)} octets : trop peu pour {w}x{h} en RAW10 compacté')
out = bytearray(w * h * 2)
total = 0
for y in range(h):
    line = d[y * stride:y * stride + w * 5 // 4]
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
print(f'{w}x{h}, pas {stride} octets, moyenne {total / (w * h):.1f} sur 1023 -> {dst}')
