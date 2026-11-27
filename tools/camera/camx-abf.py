#!/usr/bin/python3 -I
"""Réglages du débruitage du BPS (BPC-ABF 4.1.1, BPSBPCABF411Titan680) calculés par le code
de CamX lui-même, à partir du fichier de réglage du capteur.

  camx-abf.py camera.qcom.so TUNED.bin --liste
      instances du module bpcabf41_bps_v2 et leurs niveaux de gain
  camx-abf.py camera.qcom.so TUNED.bin --gain 4 [--instance N] [--noir 64] [--taille 4096x3072]
              [--force F] [--table abf.bin] [-v]
      affiche les --regs (23 registres de 0x3c68 à 0x3ce8) et écrit la table de bruit
      (64 mots, DMI 0x3c08) ; --iq affiche les octets de BpsIQSettings à mettre

Méthode : la région du fichier de réglage (bpcabf41_rgn_dataType, 107 flottants) est
interpolée selon le gain du capteur, comme CamX (gain DRC 1, rapport HDR 1), puis passée à
BPCABF411Setting::CalculateHWSetting de camera.qcom.so, exécuté dans unicorn. Le résultat
(BPCABF411UnpackedField) est rangé dans les registres comme le fait
BPSBPCABF411Titan680::PackIQRegisterSetting (relevé à la main, voir docs/isp-materiel.md).

--force multiplie la table d'écart-type du bruit (noise_stdlut_level_tab) avant le calcul :
< 1 débruite moins, > 1 davantage.

Ni camera.qcom.so ni le fichier de réglage ne vont dans le dépôt ; les valeurs produites
viennent du réglage du fabricant : les garder en local.
Dépendances : pip install unicorn pyelftools
"""
import argparse
import importlib.util
import os
import struct
import sys

from unicorn.arm64_const import (UC_ARM64_REG_S0, UC_ARM64_REG_Q0, UC_ARM64_REG_D0,
                                 UC_ARM64_REG_X0, UC_ARM64_REG_PC, UC_ARM64_REG_LR)

ICI = os.path.dirname(os.path.abspath(__file__))


def charger(nom, fichier):
    spec = importlib.util.spec_from_file_location(nom, os.path.join(ICI, fichier))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


strip = charger('bps_striping', 'bps-striping.py')
cc13 = charger('chromatix_cc13', 'chromatix-cc13.py')

HW_SETTING = 0xd4b218        # BPCABF411Setting::CalculateHWSetting
RGN_TAILLE = 0x1ac
TITAN_680 = 0x60800


def quad_depuis_double(x):
    b, = struct.unpack('<Q', struct.pack('<d', x))
    s, e, m = b >> 63, (b >> 52) & 0x7ff, b & ((1 << 52) - 1)
    if e == 0 and m == 0:
        return s << 127
    return (s << 127) | ((e - 1023 + 16383) << 112) | (m << 60)


def double_depuis_quad(q):
    s, e, m = q >> 127, (q >> 112) & 0x7fff, q & ((1 << 112) - 1)
    if e == 0 and m == 0:
        return -0.0 if s else 0.0
    b = (s << 63) | ((e - 16383 + 1023) << 52) | (m >> 60)
    return struct.unpack('<d', struct.pack('<Q', b))[0]


class Emu(strip.Emu):
    def _hook(self, u, addr, size, _):
        import math
        name = self.stubs.get(addr)
        if name == 'logf':
            v = struct.unpack('<f', struct.pack('<I', u.reg_read(UC_ARM64_REG_S0) & 0xffffffff))[0]
            r = math.log(v) if v > 0 else float('-inf')
            u.reg_write(UC_ARM64_REG_S0, struct.unpack('<I', struct.pack('<f', r))[0])
        elif name == '__extenddftf2':
            d = struct.unpack('<d', struct.pack('<Q', u.reg_read(UC_ARM64_REG_D0)))[0]
            u.reg_write(UC_ARM64_REG_Q0, quad_depuis_double(d))
        elif name == 'llroundl':
            v = double_depuis_quad(u.reg_read(UC_ARM64_REG_Q0))
            r = int(math.floor(abs(v) + 0.5)) * (1 if v >= 0 else -1)
            u.reg_write(UC_ARM64_REG_X0, r & ((1 << 64) - 1))
        else:
            return super()._hook(u, addr, size, _)
        u.reg_write(UC_ARM64_REG_PC, u.reg_read(UC_ARM64_REG_LR))


def instance(t, inst):
    for nid in t.order:
        n = t.nodes[nid]
        if n[0] == 'bpcabf41_bps_v2' and n[1][2] == inst:
            b = t.raw(nid)
            return b, struct.unpack_from('<I', b, len(b) - 4)[0]
    sys.exit(f'instance {inst} absente')


def calculer(lib, t, a):
    noeud, racine = instance(t, a.instance)
    rgn = t.interp(racine, [1.0, 1.0, a.gain])
    if len(rgn) * 4 != RGN_TAILLE:
        sys.exit(f'région de {len(rgn) * 4} octets, {RGN_TAILLE} attendus')
    rgn = list(rgn)
    for i in range(7, 7 + 65):       # noise_stdlut_level_tab
        rgn[i] *= a.force
    reserve = noeud[26 * 4:32 * 4]   # cross_process_en, ancres radiales (0, .4, .6, .8, 1)

    e = Emu(lib, verbose=a.verbose)
    w, h = (int(x) for x in a.taille.lower().split('x'))
    inp = bytearray(0x1000)
    struct.pack_into('<III', inp, 0x14, 1, 0, 0)          # ABF oui, BPC et BPC simple non
    struct.pack_into('<B', inp, 0x24, 0)                  # banque de table 0
    struct.pack_into('<H', inp, 0x28, w)                  # largeur, hauteur (centre du radial)
    struct.pack_into('<H', inp, 0x2c, h)
    struct.pack_into('<I', inp, 0x30, round(a.noir * 64))  # noir à 16 bits
    struct.pack_into('<h', inp, 0x34, 0)                  # début de l'image (recadrage)
    struct.pack_into('<h', inp, 0x38, 0)
    struct.pack_into('<B', inp, 0x58, 3)                  # GBRG
    struct.pack_into('<I', inp, 0x60, TITAN_680)
    p_in = e.put(inp)
    p_rgn = e.put(struct.pack(f'<{len(rgn)}f', *rgn))
    p_en = e.put(bytes(64))
    p_res = e.put(reserve + bytes(64))
    p_out = e.alloc(0x800)
    r = e.call(strip.BASE + HW_SETTING, p_in, p_rgn, p_en, p_res, p_out)
    if r & 0xffffffff not in (0, 1):
        sys.exit(f'CalculateHWSetting a renvoyé {r:#x}')
    return bytes(e.u.mem_read(p_out, 0x800)), rgn


def u16(o, off):
    return struct.unpack_from('<H', o, off)[0]


def u32(o, off):
    return struct.unpack_from('<I', o, off)[0]


def registres(o):
    """Rangement de BPSBPCABF411Titan680::PackIQRegisterSetting (champ déballé → mots)."""
    h = lambda off: u16(o, off)
    w = [0] * 23
    w[0] = (h(0xc) & 1) | (h(0xa) & 1) << 1 | (h(8) & 1) << 2          # 0x3c68 BPC
    w[1] = h(0xe) & 1                                                 # 0x3c6c ABF
    w[2] = (u32(o, 0x14) & 0x7ffff) << 13                             # 0x3c80 détection 0
    w[3] = (u32(o, 0x10) & 0x7ffff) << 13                             # 0x3c84 détection 1
    w[4] = (h(0x18) & 0x7f) | (h(0x1a) & 0x7f) << 16                  # 0x3c88 détection 2
    w[5] = (u32(o, 0x1c) & 0x1ffff) << 15                             # 0x3c8c récupération
    w[6] = (h(0x20) & 0x3f) | (h(0x24) & 0x3f) << 8 | (h(0x22) & 0xf) << 16 | (h(0x28) & 0xf) << 24
    w[7] = h(0x30) << 16                                              # 0x3ca4
    w[8] = h(0x2c) << 16                                              # 0x3ca8
    w[9] = ((h(0x3a) & 7) | (h(0x3c) & 3) << 4 | (h(0x3e) & 3) << 8 | (h(0x2a4) & 7) << 12 |
            (h(0x34) & 7) << 16 | (h(0x36) & 3) << 20 | (h(0x38) & 3) << 24 | (h(0x2a2) & 7) << 28)
    w[10] = (h(0x40) & 0x7f) | (h(0x42) & 0x7f) << 8 | (h(0x44) & 0x7f) << 16 | (h(0x46) & 0x7f) << 24
    w[11] = (h(0x4a) & 0x3ff) | (h(0x48) & 0x3ff) << 16
    w[12] = (h(0x50) & 0x3ff) | (h(0x4c) & 0x1ff) << 16 | (h(0x54) & 0xf) << 28
    w[13] = (h(0x52) & 0x3ff) | (h(0x4e) & 0x1ff) << 16 | (h(0x56) & 0xf) << 28
    w[14] = (h(0x58) & 0xfff) | (h(0x5a) & 0xfff) << 16
    w[15] = (h(0x5c) & 0xfff) | (h(0x5e) & 0xfff) << 16
    for k, base in enumerate((0x60, 0x68, 0x70, 0x78)):
        w[16 + k] = (o[base] | (h(base + 2) & 0xff) << 8 | o[base + 4] << 16 | (h(base + 6) & 0xff) << 24)
    w[20] = sum((h(0x80 + 2 * i) & 0xf) << (4 * i) for i in range(8))
    w[21] = (h(0x90) & 0x3fff) | (h(0x92) & 0x3fff) << 16
    w[22] = (u32(o, 0x94) & 0xfffffff) | (h(0x98) & 0xf) << 28
    adr = [0x3c68, 0x3c6c] + [0x3c80 + 4 * i for i in range(4)] + \
          [0x3ca0 + 4 * i for i in range(8)] + [0x3cc8 + 4 * i for i in range(9)]
    return list(zip(adr, w))


def table(o):
    banque = u16(o, 0x26) & 1
    return o[0x9c + banque * 0x100:0x9c + banque * 0x100 + 0x100]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('lib')
    ap.add_argument('tuned')
    ap.add_argument('--liste', action='store_true')
    ap.add_argument('--instance', type=int, default=65)
    ap.add_argument('--gain', type=float, default=1.0, help='gain total du capteur (1 = ISO de base)')
    ap.add_argument('--noir', type=float, default=64)
    ap.add_argument('--taille', default='4096x3072')
    ap.add_argument('--force', type=float, default=1.0)
    ap.add_argument('--table')
    ap.add_argument('--iq', action='store_true')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()

    t = cc13.Tuned(a.tuned)
    if a.liste:
        for nid in t.order:
            n = t.nodes[nid]
            if n[0] == 'bpcabf41_bps_v2':
                b = t.raw(nid)
                racine = struct.unpack_from('<I', b, len(b) - 4)[0]
                gains = sorted({p[-1] for p, _ in t.leaves(racine)})
                print(n[1][2], len(list(t.leaves(racine))), 'régions ; gains',
                      ' '.join(f'{s:g}-{e:g}' for s, e in gains))
        return
    if a.iq:
        print('384,1,388,0,392,1,396,0')
        return
    o, rgn = calculer(a.lib, t, a)
    if a.verbose:
        print('région : sbpc', [round(x, 3) for x in rgn[:7]], file=sys.stderr)
        print('  bruit', [round(x, 2) for x in rgn[7:72:8]], file=sys.stderr)
        print('  noyau', rgn[72:78], 'courbe', rgn[78:82], 'préservation', rgn[82:88],
              'radial', rgn[88:98], 'douceur', rgn[98], file=sys.stderr)
        print('table de bruit :', [u32(o, 0x9c + 4 * i) for i in range(0, 64, 8)], file=sys.stderr)
    if a.table:
        open(a.table, 'wb').write(table(o))
    print(','.join(f'{ad:#x},{v:#x}' for ad, v in registres(o)))


if __name__ == '__main__':
    main()
