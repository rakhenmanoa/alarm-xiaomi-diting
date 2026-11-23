#!/usr/bin/env python3
"""Registres de la matrice de couleurs du BPS (CC 1.3.1, Titan 680, 10 registres à 0x4b68)
à partir d'une matrice 3x3 en ordre RVB (lignes : sortie R, V, B ; colonnes : entrée R, V, B),
comme dans les fichiers de réglage de CamX. Format lu dans BPSCC131Titan680::PackIQRegisterSetting
et CC131Setting::CalculateHWSetting : coefficients signés 12 bits, échelle 2^(7+q) ; registres
groupés par colonne d'entrée dans l'ordre matériel V, B, R, et dans chaque groupe les sorties
V, B, R. Décalages nuls.

Usage : bps-ccm.py c0 c1 ... c8 [q=2]   (affiche la liste adresse,valeur pour icp_selftest_regs)"""
import sys

c = [float(v) for v in sys.argv[1:10]]
q = int(sys.argv[10]) if len(sys.argv) > 10 else 2
s = 1 << (7 + q)
f = [max(-2048, min(2047, round(v * s))) & 0xfff for v in c]
# c index = 3 * ligne + colonne, en RVB
regs = [
    f[4] | f[7] << 16, f[1],	# colonne V : sorties V, B | R
    f[5] | f[8] << 16, f[2],	# colonne B
    f[3] | f[6] << 16, f[0],	# colonne R
    0, 0, 0,			# décalages
    q,
]
print(','.join(f'{0x4b68 + 4 * i:#x},{v:#x}' for i, v in enumerate(regs)))
