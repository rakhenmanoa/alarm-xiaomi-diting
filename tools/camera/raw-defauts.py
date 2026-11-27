#!/usr/bin/env python3
"""Cherche les pixels défectueux et les pixels de phase (PDAF) dans une image brute RAW10.

Chaque pixel est comparé à la médiane de ses 8 voisins du même canal Bayer. Un pixel qui
s'en écarte de plus de --seuil (proportion) est noté. Les pixels de phase forment un motif
régulier : on cherche la période (en x et en y) qui regroupe le plus de pixels notés,
puis on liste les positions du motif dans une tuile de cette période.

Sur une image uniforme (papier contre l'objectif), les pixels de phase ressortent
(ils reçoivent environ la moitié de la lumière) ; sur une image noire, les pixels chauds.

Usage :
  raw-defauts.py IMAGE.raw 4096x3072 [--pas 5120] [--seuil 0.25] [--noir 64]
"""
import argparse
import numpy as np


def raw10(path, w, h, pas):
    b = np.fromfile(path, dtype=np.uint8)[:pas * h].reshape(h, pas)[:, :w * 5 // 4]
    b = b.reshape(h, w // 4, 5).astype(np.uint16)
    out = np.empty((h, w // 4, 4), np.uint16)
    for i in range(4):
        out[..., i] = (b[..., i] << 2) | ((b[..., 4] >> (2 * i)) & 3)
    return out.reshape(h, w)


def mediane_voisins(c):
    p = np.pad(c, 1, mode='edge')
    h, w = c.shape
    v = [p[1 + dy:1 + dy + h, 1 + dx:1 + dx + w]
         for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dy or dx]
    return np.median(np.stack(v), axis=0)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image')
    ap.add_argument('taille')
    ap.add_argument('--pas', type=int)
    ap.add_argument('--seuil', type=float, default=0.25)
    ap.add_argument('--noir', type=float, default=64)
    ap.add_argument('--abs', type=float, default=40,
                    help='écart minimal en niveaux 10 bits (évite le bruit dans le noir)')
    a = ap.parse_args()
    w, h = (int(x) for x in a.taille.lower().split('x'))
    img = raw10(a.image, w, h, a.pas or w * 5 // 4).astype(np.float32) - a.noir
    print(f'moyenne {img.mean() + a.noir:.1f}, min {img.min() + a.noir:.0f}, '
          f'max {img.max() + a.noir:.0f} (10 bits, noir {a.noir:g})')

    ys, xs, rs = [], [], []
    for oy in (0, 1):
        for ox in (0, 1):
            c = img[oy::2, ox::2]
            m = mediane_voisins(c)
            ecart = c - m
            r = np.where(m > 8, c / np.maximum(m, 1), 1)
            sel = (np.abs(r - 1) > a.seuil) & (np.abs(ecart) > a.abs)
            y, x = np.nonzero(sel)
            ys.append(y * 2 + oy)
            xs.append(x * 2 + ox)
            rs.append(r[sel])
            print(f'canal ({ox},{oy}) : médiane {np.median(c) + a.noir:.0f}, '
                  f'{sel.sum()} pixels hors seuil ({sel.sum() / c.size * 1e6:.0f} par million)')
    ys, xs, rs = np.concatenate(ys), np.concatenate(xs), np.concatenate(rs)
    n = len(xs)
    print(f'total {n} pixels notés, {(rs < 1).sum()} trop sombres, {(rs > 1).sum()} trop clairs')
    if n < 10:
        return

    # période qui concentre le plus de pixels notés dans le moins de positions
    best = None
    for p in (8, 16, 32, 64):
        pos, cnt = np.unique((ys % p) * p + (xs % p), return_counts=True)
        part = np.sort(cnt)[::-1]
        k = min(len(part), max(1, p * p // 64))
        conc = part[:k].sum() / n
        print(f'période {p:2d} : {len(pos)} positions occupées, les {k} premières '
              f'regroupent {conc * 100:.0f} %')
        if best is None or conc > best[1] + 0.05:
            best = (p, conc, pos, cnt)
    p, conc, pos, cnt = best
    if conc < 0.5:
        print('pas de motif régulier net : pixels isolés (défauts) plutôt que pixels de phase')
        return
    print(f'\nmotif de période {p} : positions (x, y) dans la tuile, nombre, rapport moyen')
    ordre = np.argsort(cnt)[::-1]
    lim = cnt[ordre[0]] * 0.3
    for i in ordre:
        if cnt[i] < lim:
            break
        py, px = divmod(pos[i], p)
        sel = (ys % p == py) & (xs % p == px)
        print(f'  ({px:2d}, {py:2d})  {cnt[i]:7d}  {rs[sel].mean():.2f}')


if __name__ == '__main__':
    main()
