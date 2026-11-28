#!/usr/bin/python3 -I
"""Table des matrices de couleurs selon l'éclairage pour libcamera (patch 0025) : matrices CC 1.3
d'un fichier de réglage CamX interpolées sur une grille indice de luminosité × température de
couleur, et lieu des corps noirs pour estimer la température à partir des gains de balance des
blancs.

  bps-ccm-table.py TUNED.bin bps-ccm.bin [--instance 929] [--lux-min 195]

--lux-min : plus petit indice de luminosité de la grille (195). Les scènes plus lumineuses prennent
            les matrices de cet indice : la région « plein soleil » du HP1 (indice 1-145) est
            nettement plus saturée, ce qui s'ajoute à la saturation de la conversion YUV
            (bps-bloc.py --saturation).

Format (petit-boutiste) : « CCMT », u32 version 1, u32 nLux, u32 nCct, u32 nLieu ;
f32 lux[nLux] (croissant) ; f32 mired[nCct] (croissant) ; nLieu × f32 {log R/V, log B/V, mired} ;
nLux × nCct × 9 f32 (matrice RVB par lignes). Contient des réglages du fabricant : à garder hors
du dépôt (installer dans /lib/firmware/bps-ccm.bin).
"""
import argparse
import importlib.util
import math
import os
import struct

spec = importlib.util.spec_from_file_location(
    'cc13', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'chromatix-cc13.py'))
cc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cc)

LUX = [80, 155, 195, 235, 285, 335, 435, 560, 650]
CCT = [7500, 6500, 5800, 5550, 5300, 5000, 4700, 4450, 4200, 3900, 3600, 3400, 3200, 3000,
       2800, 2650, 2500, 2300]


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('tuned')
    p.add_argument('sortie')
    p.add_argument('--instance', type=int, default=929)
    p.add_argument('--lux-min', type=float, default=195)
    a = p.parse_args()

    t = cc.Tuned(a.tuned)
    root = [r for name, inst, r in t.modules('cc13_') if inst == a.instance][0]
    n = t.depth(root)
    lux = [x for x in LUX if x >= a.lux_min]
    pts = cc.ref_points(t)
    lieu = [(math.log(pts[i][0]), math.log(pts[i][1]), 1e6 / k) for i, k in cc.LIEU]

    out = b'CCMT' + struct.pack('<4I', 1, len(lux), len(CCT), len(lieu))
    out += struct.pack(f'<{len(lux)}f', *lux)
    out += struct.pack(f'<{len(CCT)}f', *[1e6 / k for k in CCT])
    for x, y, m in lieu:
        out += struct.pack('<3f', x, y, m)
    for lx in lux:
        for k in CCT:
            m = t.interp(root, [1.0, 1.0, 0.0, lx, k][-n:])
            out += struct.pack('<9f', *m[:9])
    open(a.sortie, 'wb').write(out)
    for lx in (lux[0], lux[len(lux) // 2], lux[-1]):
        for k in (6500, 4200, 2800):
            m = t.interp(root, [1.0, 1.0, 0.0, lx, k][-n:])
            print(f'indice {lx:g}, {k} K : diagonale {m[0]:.3f} {m[4]:.3f} {m[8]:.3f}')
    print(f'{a.sortie} : {len(out)} octets, {len(lux)} × {len(CCT)} matrices')


if __name__ == '__main__':
    main()
