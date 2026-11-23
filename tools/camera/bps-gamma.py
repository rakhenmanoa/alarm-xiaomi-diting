#!/usr/bin/env python3
"""Écrit les 3 tables gamma du BPS (module GLUT, Titan 680) pour icp_selftest_glut_file.
Chaque table : 256 entrées de 32 bits, valeur sur 12 bits (bits 0-11) et écart avec l'entrée
suivante, signé sur 12 bits (bits 12-23), format lu dans Gamma15Setting::CalculateHWSetting de
CamX. Les 3 tables (sélecteurs DMI 1, 2, 3) sont identiques ici.

Usage : bps-gamma.py sortie.bin [gamma=2.2] [gain=1.0]"""
import struct, sys

out = sys.argv[1]
gamma = float(sys.argv[2]) if len(sys.argv) > 2 else 2.2
gain = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
vals = [min(4095, round(4095 * min(1.0, gain * i / 255) ** (1 / gamma))) for i in range(256)]
table = b''
for i, v in enumerate(vals):
    nxt = vals[i + 1] if i < 255 else 4096
    delta = max(-2048, min(2047, nxt - v)) & 0xfff
    table += struct.pack('<I', (delta << 12) | v)
open(out, 'wb').write(table * 3)
print(f'gamma {gamma}, gain {gain} : entrées 0, 16, 64, 128, 255 = '
      f'{vals[0]}, {vals[16]}, {vals[64]}, {vals[128]}, {vals[255]} -> {out}')
