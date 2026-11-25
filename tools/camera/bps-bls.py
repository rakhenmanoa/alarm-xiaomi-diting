#!/usr/bin/env python3
"""Registres de la soustraction du noir du BPS (BLS 1.2.1, Titan 680, 6 registres à 0x3e68).

Format lu dans BPSBLS121Titan680::PackIQRegisterSetting, BLS121Setting::CalculateHWSetting et
DumpRegConfig (camera.qcom.so, sauvegarde HyperOS) :
  0x3e68  « Config register 0 »  niveau de noir << 14 (champ de 18 bits, bits 14-31)
  0x3e6c  « Config register 1 »  gain 2^N / (2^N - noir), virgule fixe Q11 (17 bits, 0-16),
                                  borné à [0, 0x1ffff] (1,0 = 0x800)
  0x3e70..0x3e7c  « Threshold config 0..3 » seuils de noir par canal, champ de 18 bits (bits 14-31) :
                                  sous le seuil, l'échantillon brut du canal est forcé à 0 (mesuré le
                                  25 nov. 2026). Ordre : 0x3e70 vert Gr, 0x3e74 vert Gb, 0x3e78 bleu,
                                  0x3e7c rouge (R, Gr, Gb, B dans CamX). Même échelle que le décalage
                                  (N = 17). À laisser à 0x4000 (sans effet) : un seuil plus haut, même
                                  égal sur les quatre canaux, crée du bruit coloré dans les ombres
                                  (rouge, turquoise) ; 0x8000 écrase les ombres, 0x1ffff tout.
N est l'échelle interne du décalage ; le noir mesuré sur 10 bits est multiplié par 2^(N-10).
Mesuré sur le téléphone le 25 nov. 2026 (HP1, photo de test, gamma sans noir) : le décalage doit
être donné à l'échelle N = 17 (noir de 67 sur 10 bits -> 8576) ; à N = 14 il est sans effet, à N = 18
il est deux fois trop fort.
Le noir mesuré sur 10 bits est multiplié par 2^(N-10). L'interrupteur est l'octet 1060 de
BpsIQSettings (--iq 1060,1, et le découpage doit être recalculé avec cet interrupteur).

Usage : bps-bls.py NOIR10 [N=17] [SEUIL10] [S0,S1,S2,S3]   (affiche la liste adresse,valeur pour --regs)
        NOIR10 : niveau de noir sur 10 bits (ex. 67)"""
import sys

noir10 = float(sys.argv[1])
n = int(sys.argv[2]) if len(sys.argv) > 2 else 17
full = 1 << n
noir = min(round(noir10 * (full >> 10)), full - 1)
gain = max(0, min(0x1ffff, int(full / (full - noir) * 2048)))   # Q11
# seuils : plafonnés à 14 bits (0x3fff) ; plus hauts (N = 17 ou 18), ils écrasent toute l'image
s10 = float(sys.argv[3]) if len(sys.argv) > 3 else None
seuil = (0x4000 if s10 is None else round(s10 * (full >> 10))) << 14 & 0xffffffff
if len(sys.argv) > 4:   # SEUILS : quatre valeurs de champ (18 bits, non décalées) pour 0x3e70,74,78,7c
    t = [int(v, 0) << 14 & 0xffffffff for v in sys.argv[4].split(',')]
else:
    t = [seuil] * 4
regs = {
    0x3e68: noir << 14 & 0xffffffff,
    0x3e6c: gain,
    0x3e70: t[0], 0x3e74: t[1], 0x3e78: t[2], 0x3e7c: t[3],
}
print(','.join(f'{a:#x},{v:#x}' for a, v in sorted(regs.items())))
