#!/usr/bin/env python3
"""Écrit les 3 tables gamma du BPS (module GLUT, Titan 680).
Chaque table : 256 entrées de 32 bits, valeur sur 12 bits (bits 0-11) et écart avec l'entrée
suivante, signé sur 12 bits (bits 12-23), format lu dans Gamma15Setting::CalculateHWSetting de
CamX. Les 3 tables (sélecteurs DMI 1, 2, 3) sont identiques ici.

Usage : bps-gamma.py sortie.bin [gamma=2.2] [gain=1.0] [noir=0]
            [--srgb] [--coupe C] [--contraste A]

noir        niveau de noir du capteur sur 10 bits (par ex. 64), retiré avant la courbe ; 0
            quand le module BLS le retire déjà
--srgb      courbe sRGB (partie linéaire près du noir, puis puissance 1/2,4) au lieu de la
            puissance pure 1/gamma, dont la pente presque verticale au noir change le bruit
            des ombres en gris clair
--coupe C   écrase le noir : les C premiers 1/255 de l'entrée donnent 0 (bruit des ombres)
--contraste A  courbe en S sur la sortie, mélange A de smoothstep (0 = aucune, 0,3 = net)
"""
import struct
import sys


def srgb(x):
    return 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055


def main():
    args, opts, i = [], {}, 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == '--srgb':
            opts['srgb'] = True
        elif a in ('--coupe', '--contraste'):
            i += 1
            opts[a[2:]] = float(sys.argv[i])
        else:
            args.append(a)
        i += 1
    if not args:
        sys.exit(__doc__)
    out = args[0]
    gamma = float(args[1]) if len(args) > 1 else 2.2
    gain = float(args[2]) if len(args) > 2 else 1.0
    black = float(args[3]) / 1023 if len(args) > 3 else 0.0
    coupe = opts.get('coupe', 0.0) / 255
    contraste = opts.get('contraste', 0.0)

    vals = []
    for i in range(256):
        x = max(0.0, i / 255 - black) / (1 - black)
        x = min(1.0, gain * max(0.0, x - coupe) / (1 - coupe))
        y = srgb(x) if opts.get('srgb') else x ** (1 / gamma)
        y = (1 - contraste) * y + contraste * y * y * (3 - 2 * y)
        vals.append(min(4095, round(4095 * y)))

    table = b''
    for i, v in enumerate(vals):
        nxt = vals[i + 1] if i < 255 else 4096
        delta = max(-2048, min(2047, nxt - v)) & 0xfff
        table += struct.pack('<I', (delta << 12) | v)
    open(out, 'wb').write(table * 3)
    print(f'{"sRGB" if opts.get("srgb") else f"gamma {gamma}"}, gain {gain}, noir {black * 1023:.0f}, '
          f'coupe {coupe * 255:g}, contraste {contraste:g} : entrées 0, 1, 2, 4, 16, 64, 128, 255 = '
          + ', '.join(str(vals[k]) for k in (0, 1, 2, 4, 16, 64, 128, 255)) + f' -> {out}')


if __name__ == '__main__':
    main()
