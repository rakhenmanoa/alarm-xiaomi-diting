#!/usr/bin/python3 -I
"""Moyenne de chaque position du motif de Bayer (2 x 2) d'une image RAW10 compactée
(MIPI, 4 pixels sur 5 octets), dans des rectangles : sert à vérifier l'ordre Bayer et
que les quatre positions portent bien des couleurs différentes.

  raw10-phases.py IMAGE.bin LARGEUR HAUTEUR [x0,x1,y0,y1 ...] [--pas OCTETS] [--noir 64]

Sans rectangle : l'image entière. Affiche pour chaque rectangle les moyennes des
positions (0,0) (0,1) (1,0) (1,1), noir retiré (64 : mesuré le 27 nov. 2026 sur une
image noire du HP1, identique sur les quatre positions ; --noir 0 pour le lire). Pour un capteur GBRG : G B / R G.
"""
import argparse

p = argparse.ArgumentParser()
p.add_argument('image')
p.add_argument('largeur', type=int)
p.add_argument('hauteur', type=int)
p.add_argument('rect', nargs='*')
p.add_argument('--pas', type=int, help='octets par ligne (défaut : largeur × 5/4)')
p.add_argument('--noir', type=float, default=64)
a = p.parse_args()

w, h = a.largeur, a.hauteur
pas = a.pas or w * 5 // 4
d = open(a.image, 'rb').read()


def ligne(y):
    o = y * pas
    b = d[o:o + w * 5 // 4]
    px = []
    for i in range(0, len(b), 5):
        lo = b[i + 4]
        px += [(b[i] << 2) | (lo & 3), (b[i + 1] << 2) | (lo >> 2 & 3),
               (b[i + 2] << 2) | (lo >> 4 & 3), (b[i + 3] << 2) | (lo >> 6 & 3)]
    return px


rects = [tuple(int(v) for v in r.split(',')) for r in a.rect] or [(0, w, 0, h)]
for x0, x1, y0, y1 in rects:
    x0, y0 = x0 & ~1, y0 & ~1
    s = [0.0] * 4
    n = [0] * 4
    for y in range(y0, y1):
        px = ligne(y)
        for x in range(x0, x1):
            k = (y & 1) * 2 + (x & 1)
            s[k] += px[x] - a.noir
            n[k] += 1
    m = [s[k] / max(n[k], 1) for k in range(4)]
    print(f'{x0},{x1},{y0},{y1} : (0,0) {m[0]:7.1f}  (0,1) {m[1]:7.1f}  '
          f'(1,0) {m[2]:7.1f}  (1,1) {m[3]:7.1f}')
