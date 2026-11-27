#!/usr/bin/env python3
"""Correction d'ombrage de l'objectif (LSC 4.1.1, BPSLSC411Titan680) à partir de l'EEPROM du HP1.

Le module applique, sur chaque canal Bayer, un gain interpolé sur une grille de
17 x 13 points (16 x 12 blocs), dans le domaine Bayer (demi-largeur, demi-hauteur).
Relevé dans camera.qcom.so (CreateCmdList, PackIQRegisterSetting, UpdateFirmwareData)
et dans le DWARF du firmware de l'ICP (LensRollOffParameters_480) :

- tables (3 × 221 mots de 32 bits, DMI à 0x3608, sélecteurs 1 à 3, programme 5
  BPS_LENS_ROLLOFF_LUT_PROGRAM) : R | Gr << 14, puis B | Gb << 14, puis ALSC (à 0) ;
  gains sur 14 bits, 1,0 = 1 024, rangés ligne par ligne ;
- 13 registres à 0x3668 (voir regs()) ;
- BpsIQSettings, octets 444 à 488 : EN, ALSC_EN, NUM_SUBBLOCKS, largeur de sous-bloc − 1,
  largeur de bloc − 1, indices de départ (0 pour une image entière), nombre de gains
  horizontaux − 2.

Usage :
  bps-lsc.py EEPROM.bin 4096x3072 --ajouter ANCIEN.bin NOUVEAU.bin --sl sl-avec-lsc.bin
  bps-lsc.py EEPROM.bin 4096x3072 --table lsc.bin     # écrit les tables, affiche les --regs
  bps-lsc.py EEPROM.bin 4096x3072 --iq                # affiche les --iq à ajouter
Options : --force F (part de la chute de lumière corrigée, 1 = toute, défaut 0.85 ;
la teinte des coins est toujours corrigée entièrement), --noir N (noir sur 10 bits
retiré avant le gain puis remis, défaut 64), --sans-couleur (même gain sur les 4 canaux).

Hypothèses à vérifier sur image : grille de l'EEPROM étalée sur tout le champ ;
canaux de l'EEPROM et des tables dans l'ordre R, Gr, Gb, B ; x_delta = 2^20 / largeur de
sous-bloc ; décalages à l'échelle 18 bits (noir10 × 256, comme la balance des blancs)."""
import argparse
import math
import struct
import sys

GW, GH = 17, 13          # points de la grille
EE_LSC = 0x67            # début de la table dans l'EEPROM


def eeprom_lsc(b):
    v = [(b[EE_LSC + 2 * i] << 8) | b[EE_LSC + 2 * i + 1] for i in range(GW * GH * 4)]
    return [[[v[(y * GW + x) * 4 + c] for x in range(GW)] for y in range(GH)] for c in range(4)]


def geometrie(w, h):
    wb, hb = w // 2, h // 2
    for intp in (3, 2, 1, 0):
        sw = math.ceil(wb / ((GW - 1) << intp))
        sh = math.ceil(hb / ((GH - 1) << intp))
        if sw >= 9 and sh >= 9:
            break
    if sw > 2048 or sh > 1024:
        sys.exit('image trop grande pour la grille')
    return intp, sw, sh, sw << intp, sh << intp


def bilin(g, u, v):
    u = min(max(u, 0.0), GW - 1.0)
    v = min(max(v, 0.0), GH - 1.0)
    x0, y0 = min(int(u), GW - 2), min(int(v), GH - 2)
    fx, fy = u - x0, v - y0
    return ((g[y0][x0] * (1 - fx) + g[y0][x0 + 1] * fx) * (1 - fy)
            + (g[y0 + 1][x0] * (1 - fx) + g[y0 + 1][x0 + 1] * fx) * fy)


def gains(ee, w, h, force, couleur):
    """Gains (R, Gr, Gb, B) aux nœuds de la grille matérielle, 1,0 = 1 024."""
    _, _, _, bw, bh = geometrie(w, h)
    wb, hb = w // 2, h // 2
    centre = [g[GH // 2][GW // 2] for g in ee]
    out = [[[0] * GW for _ in range(GH)] for _ in range(4)]
    for j in range(GH):
        for i in range(GW):
            # position du nœud matériel rapportée à la grille de l'EEPROM (champ entier)
            u, v = i * bw * (GW - 1) / wb, j * bh * (GH - 1) / hb
            g = [centre[c] / bilin(ee[c], u, v) for c in range(4)]
            gv = (g[1] + g[2]) / 2
            lum = gv ** force
            for c in range(4):
                val = lum * (g[c] / gv if couleur else 1.0)
                out[c][j][i] = min(0x3fff, round(val * 1024))
    return out


def tables(gn):
    r, gr, gb, b = gn
    t1, t2 = [], []
    for j in range(GH):
        for i in range(GW):
            t1.append(r[j][i] | gr[j][i] << 14)
            t2.append(b[j][i] | gb[j][i] << 14)
    return struct.pack(f'<{3 * GW * GH}I', *t1, *t2, *([0] * (GW * GH)))


def regs(w, h, noir):
    intp, sw, sh, bw, bh = geometrie(w, h)
    xd, yd = round((1 << 20) / sw), round((1 << 20) / sh)
    off = min(0x3ffff, round(noir * 256)) << 14 & 0xffffffff
    v = [
        (GW - 2) | (GH - 2) << 4,        # 0x3668 nombre de gains − 2
        (bw - 1) | (bh - 1) << 16,       # 0x366c taille de bloc − 1
        (sw - 1) | xd << 12,             # 0x3670 largeur de sous-bloc − 1, x_delta
        (sh - 1) | yd << 12,             # 0x3674 hauteur de sous-bloc − 1, y_delta
        0,                               # 0x3678 Bx_d1, By_e1 (départ)
        off,                             # 0x367c décalage remis après le gain
        0,                               # 0x3680 By_init_e1
        0, 0,                            # 0x3684, 0x3688 pondération de luminance (ALSC)
        0, 0,                            # 0x368c, 0x3690 recadrage (inactif)
        off,                             # 0x3694 décalage retiré avant le gain
        0,                               # 0x3698 (ALSC)
    ]
    return [x for k, val in enumerate(v) for x in (0x3668 + 4 * k, val)]


def iq(w, h):
    intp, sw, sh, bw, bh = geometrie(w, h)
    vals = [1, 0, intp, sw - 1, bw - 1, 0, 0, 0, GW - 2, 0, 0, 0]
    return [x for k, val in enumerate(vals) for x in (444 + 4 * k, val)]


def ajouter(a, w, h):
    """Reprend un bloc existant (version 1 ou 2) : octets 444-488, registres, tables, découpage."""
    if not a.sl:
        sys.exit('--ajouter demande --sl (découpage calculé avec les mêmes --iq et le LSC)')
    blob = open(a.ajouter[0], 'rb').read()
    magic, version, bw_, bh_ = struct.unpack_from('<4I', blob)
    if magic != 0x50535042 or version not in (1, 2):
        sys.exit('bloc de paramètres inconnu')
    nsec = 4 if version == 1 else 5
    sec = struct.unpack_from(f'<{2 * nsec}I', blob, 16)
    parts = [blob[o:o + n] if n else b'' for o, n in zip(sec[0::2], sec[1::2])]
    if nsec == 4:
        parts.append(b'')
    iq_b = bytearray(parts[0])
    avant = [(o, v) for o in range(0, len(iq_b) - 3, 4)
             if (v := struct.unpack_from('<I', iq_b, o)[0]) and not 444 <= o <= 488]
    print('octets de BpsIQSettings déjà mis :', ','.join(f'{o},{v}' for o, v in avant))
    if len(iq_b) < 492:
        iq_b += bytes(492 - len(iq_b))
    vals = iq(w, h)
    for off, val in zip(vals[0::2], vals[1::2]):
        struct.pack_into('<I', iq_b, off, val)
    # retire d'anciens registres LSC, puis ajoute les nouveaux
    r = list(struct.unpack(f'<{len(parts[1]) // 4}I', parts[1]))
    paires = [(r[i], r[i + 1]) for i in range(0, len(r), 2) if not 0x3668 <= r[i] <= 0x3698]
    nv = regs(w, h, a.noir)
    paires += list(zip(nv[0::2], nv[1::2]))
    regs_b = struct.pack(f'<{2 * len(paires)}I', *(x for p in paires for x in p))
    b = open(a.eeprom, 'rb').read()
    lsc_b = tables(gains(eeprom_lsc(b), w, h, a.force, not a.sans_couleur))
    sl_b = open(a.sl, 'rb').read()
    blobs = [bytes(iq_b), regs_b, parts[2], sl_b, lsc_b]
    off, data, secs = 56, b'', []
    for x in blobs:
        secs += [off if x else 0, len(x)]
        data += x + bytes(-len(x) % 4)
        off += len(x) + (-len(x) % 4)
    out = struct.pack('<4I10I', 0x50535042, 2, w, h, *secs) + data
    if len(out) > 32768:
        sys.exit(f'bloc de {len(out)} octets, 32768 au plus')
    open(a.ajouter[1], 'wb').write(out)
    print(f'{a.ajouter[1]} : {len(out)} octets, {len(paires)} registres, ombrage force {a.force}')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('eeprom')
    ap.add_argument('taille')
    ap.add_argument('--table')
    ap.add_argument('--iq', action='store_true')
    ap.add_argument('--force', type=float, default=0.85)
    ap.add_argument('--noir', type=float, default=64)
    ap.add_argument('--sans-couleur', action='store_true')
    ap.add_argument('--montrer', action='store_true', help='affiche la grille de gains (Gr)')
    ap.add_argument('--ajouter', nargs=2, metavar=('ENTREE', 'SORTIE'),
                    help="ajoute l'ombrage à un bloc de bps-params.py existant")
    ap.add_argument('--sl', help='avec --ajouter : découpage recalculé avec le LSC (obligatoire)')
    a = ap.parse_args()

    w, h = (int(x) for x in a.taille.lower().split('x'))
    if a.ajouter:
        ajouter(a, w, h)
        return
    if a.iq:
        print(','.join(str(x) for x in iq(w, h)))
        return
    b = open(a.eeprom, 'rb').read()
    gn = gains(eeprom_lsc(b), w, h, a.force, not a.sans_couleur)
    if a.montrer:
        intp, sw, sh, bw, bh = geometrie(w, h)
        print(f'sous-blocs 2^{intp}, sous-bloc {sw}x{sh}, bloc {bw}x{bh}', file=sys.stderr)
        for l in gn[1]:
            print(' '.join(f'{x / 1024:4.2f}' for x in l), file=sys.stderr)
    if a.table:
        open(a.table, 'wb').write(tables(gn))
    print(','.join(hex(x) for x in regs(w, h, a.noir)))


if __name__ == '__main__':
    main()
