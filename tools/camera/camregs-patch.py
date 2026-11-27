#!/usr/bin/env python3
"""Recopie dans une table d'un fichier camregs les valeurs d'une autre table,
pour les registres d'une ou plusieurs plages d'adresses.

  camregs-patch.py IN.camregs OUT.camregs CIBLE SOURCE PLAGE [PLAGE…]
  camregs-patch.py s5khp1-wide.camregs essai.camregs mode6 mode2 0x20010000-0x200107ff

Les adresses sont celles sur 32 bits du capteur : les capteurs Samsung
choisissent la page haute par une écriture dans 0xfcfc (0x4000 = registres
standard CCI, 0x2000… = mémoire interne). Seuls les registres écrits dans les
deux tables et de valeurs différentes sont modifiés, rafales comprises ; la
taille et l'ordre des écritures ne changent pas. Sert à tester, par petits
groupes, les réglages qui distinguent deux modes. Le fichier produit contient
des réglages du fabricant : le garder en local.
"""
import struct
import sys

PAGE = 0xfcfc


def lire(b):
    if b[:8] not in (b'CAMREGS1', b'CAMREGS2'):
        sys.exit('pas un fichier camregs')
    n, = struct.unpack_from('<I', b, 8)
    off, tables = 12, {}
    for _ in range(n):
        name = b[off:off + 16].split(b'\0')[0].decode()
        cnt, = struct.unpack_from('<I', b, off + 16)
        off += 20
        ent = []
        for _ in range(cnt):
            addr, val, width, _pad, delay = struct.unpack_from('<HHBBH', b, off)
            ent.append((off, addr, val, width))
            off += 8
            if width == 0:
                off += (val + 3) & ~3
        tables[name] = ent
    return tables


def registres(b, ent):
    """{adresse 32 bits: (valeur, taille, [(position, largeur)])}, page 0x4000 au départ."""
    page, regs = 0x4000, {}
    for off, addr, val, width in ent:
        if width == 0:
            for i in range(0, val, 2):
                a = (page << 16) | (addr + i)
                v = int.from_bytes(b[off + 8 + i:off + 10 + i], 'big') if i + 1 < val \
                    else b[off + 8 + i]
                regs[a] = (v, 2 if i + 1 < val else 1, ('rafale', off + 8 + i))
            continue
        if addr == PAGE:
            page = val
            continue
        regs[(page << 16) | addr] = (val, width, ('entree', off))
    return regs


def plage(s):
    a, _, z = s.partition('-')
    return int(a, 0), int(z or a, 0)


def main():
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    src, dst, cible, source = sys.argv[1:5]
    plages = [plage(s) for s in sys.argv[5:]]
    b = bytearray(open(src, 'rb').read())
    t = lire(b)
    rc, rs = registres(b, t[cible]), registres(b, t[source])
    n = 0
    for a in sorted(rc):
        if a not in rs or not any(x <= a <= y for x, y in plages):
            continue
        (vc, wc, (genre, pos)), (vs, _, _) = rc[a], rs[a]
        if vc == vs:
            continue
        if genre == 'entree':
            struct.pack_into('<H', b, pos + 2, vs)
        else:
            b[pos:pos + wc] = vs.to_bytes(wc, 'big')
        print(f'{a:#010x}  {vc:#06x} -> {vs:#06x}')
        n += 1
    open(dst, 'wb').write(b)
    print(f'{n} registres de {cible} remplacés par ceux de {source}', file=sys.stderr)


if __name__ == '__main__':
    main()
