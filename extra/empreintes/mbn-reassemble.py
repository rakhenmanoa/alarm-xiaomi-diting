#!/usr/bin/python3 -I
"""Réassemble un TA Qualcomm découpé (.mdt + .bNN) en un seul .mbn.

  mbn-reassemble.py /usr/lib/firmware/keymaster64.mdt keymaster64.mbn

Le .mdt contient l'en-tête ELF, la table des segments de programme et le
segment de hachage ; chaque segment i est dans le fichier voisin « base.bNN »
et se place à son p_offset. Même résultat que pil-squasher, sans dépendance.
Ne lit que du firmware déjà présent ; n'embarque aucun binaire propriétaire.
"""
import os
import struct
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    mdt, out = sys.argv[1], sys.argv[2]
    base = mdt[:-4] if mdt.endswith('.mdt') else mdt
    d = open(mdt, 'rb').read()
    if d[:4] != b'\x7fELF' or d[4] != 2:
        sys.exit('pas un ELF64')
    e_phoff, = struct.unpack_from('<Q', d, 0x20)
    e_phentsize, e_phnum = struct.unpack_from('<HH', d, 0x36)
    buf = bytearray(d)
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_offset, = struct.unpack_from('<Q', d, off + 8)
        p_filesz, = struct.unpack_from('<Q', d, off + 32)
        bfile = f'{base}.b{i:02d}'
        if p_filesz == 0 or not os.path.exists(bfile):
            continue
        seg = open(bfile, 'rb').read()
        end = p_offset + len(seg)
        if len(buf) < end:
            buf.extend(b'\0' * (end - len(buf)))
        buf[p_offset:end] = seg
    open(out, 'wb').write(buf)
    print(f'{out} : {len(buf)} octets, {e_phnum} segments')


if __name__ == '__main__':
    main()
