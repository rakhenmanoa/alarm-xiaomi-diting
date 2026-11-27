#!/usr/bin/env python3
"""Compare deux tables d'un fichier camregs (registres des modes d'un capteur).

  camregs-diff.py s5khp1.camregs mode2 mode6
  camregs-diff.py s5khp1.camregs --liste

Affiche les registres présents dans une seule des deux tables ou avec des valeurs
différentes, triés par adresse. Les registres liés à l'exposition et au gain
(0x0104 maintien, 0x0202-0x0205 pose et gain, 0x0340-0x0343 trame et ligne,
0x0702-0x0705 décalages de pose longue, 0x0e00-0x0eff modes rapides et DCG) sont marqués.
La sortie contient des réglages du fabricant : ne pas la mettre dans le dépôt.
"""
import struct
import sys


def lire(path):
    b = open(path, 'rb').read()
    magic = b[:8]
    if magic not in (b'CAMREGS1', b'CAMREGS2'):
        sys.exit('pas un fichier camregs')
    n, = struct.unpack_from('<I', b, 8)
    off, tables = 12, {}
    for _ in range(n):
        name = b[off:off + 16].split(b'\0')[0].decode()
        cnt, = struct.unpack_from('<I', b, off + 16)
        off += 20
        regs = []
        for _ in range(cnt):
            addr, val, width, _pad, delay = struct.unpack_from('<HHBBH', b, off)
            off += 8
            if width == 0:          # rafale de `val` octets
                data = b[off:off + val]
                off += (val + 3) & ~3
                for i, x in enumerate(data):
                    regs.append((addr + i, x, 1))
            else:
                regs.append((addr, val, width))
        tables[name] = regs
    return tables


def marque(a):
    if a == 0x0104 or 0x0202 <= a <= 0x0205 or 0x0340 <= a <= 0x0343 or \
       0x0702 <= a <= 0x0705 or 0x0e00 <= a <= 0x0eff:
        return ' <-'
    return ''


def main():
    t = lire(sys.argv[1])
    if len(sys.argv) < 4:
        for k, v in t.items():
            print(f'{k:8s} {len(v)} écritures')
        return
    a, b = t[sys.argv[2]], t[sys.argv[3]]
    da, db = {}, {}
    for addr, val, w in a:
        da[addr] = (val, w)        # la dernière écriture compte
    for addr, val, w in b:
        db[addr] = (val, w)
    print(f'{"adresse":8s} {sys.argv[2]:>8s} {sys.argv[3]:>8s}')
    for addr in sorted(set(da) | set(db)):
        va, vb = da.get(addr), db.get(addr)
        if va == vb:
            continue
        fa = f'{va[0]:#06x}' if va else '-'
        fb = f'{vb[0]:#06x}' if vb else '-'
        print(f'{addr:#06x}   {fa:>8s} {fb:>8s}{marque(addr)}')


if __name__ == '__main__':
    main()
