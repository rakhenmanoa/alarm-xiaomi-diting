#!/usr/bin/env python3
"""Modifie un bloc de paramètres du BPS existant (bps-params.py, versions 1 à 3).

  bps-bloc.py ENTREE.bin                          # décrit le bloc
  bps-bloc.py ENTREE.bin SORTIE.bin [--iq o,v,...] [--regs a,v,...] [--lut prog,dmi,FICHIER]...
              [--sl FICHIER] [--sans-lut PROG]

--iq     octets (mots de 32 bits) de BpsIQSettings à écrire
--regs   registres à ajouter ; un registre déjà présent est remplacé
--lut    table DMI à ajouter (section « luts », version 3) : programme (4 ABF, 6 GTM, 8 HNR…),
         registre DMI avec le sélecteur en bits 24-31 (ex. 0x01003c08), fichier de données ;
         une table de même programme et même registre est remplacée
--sans-lut PROG  retire les tables d'un programme
--sl     découpage recalculé (obligatoire dès que des octets de BpsIQSettings changent la
         géométrie des modules : LSC, ABF…)

Le bloc écrit est toujours en version 3 (le pilote accepte 1 à 3).
"""
import argparse
import struct
import sys

MAGIC = 0x50535042
NOMS = ('iq', 'regs', 'glut', 'sl', 'lsc', 'luts')


def lire(path):
    b = open(path, 'rb').read()
    magic, version, w, h = struct.unpack_from('<4I', b)
    if magic != MAGIC or not 1 <= version <= 3:
        sys.exit(f'{path} : bloc de paramètres inconnu')
    n = version + 3
    sec = struct.unpack_from(f'<{2 * n}I', b, 16)
    parts = {NOMS[i]: (b[sec[2 * i]:sec[2 * i] + sec[2 * i + 1]] if sec[2 * i + 1] else b'')
             for i in range(n)}
    for k in NOMS:
        parts.setdefault(k, b'')
    return version, w, h, parts


def luts(b):
    out, off = [], 0
    while off < len(b):
        prog, dmi, n = struct.unpack_from('<3I', b, off)
        out.append((prog, dmi, b[off + 12:off + 12 + n]))
        off += 12 + n
    return out


def ecrire(path, w, h, parts):
    blobs = [parts[k] for k in NOMS]
    hdr = 16 + 8 * len(NOMS)
    off, data, secs = hdr, b'', []
    for x in blobs:
        secs += [off if x else 0, len(x)]
        pad = bytes(-len(x) % 4)
        data += x + pad
        off += len(x) + len(pad)
    out = struct.pack(f'<4I{2 * len(NOMS)}I', MAGIC, 3, w, h, *secs) + data
    if len(out) > 32768:
        sys.exit(f'bloc de {len(out)} octets, 32768 au plus')
    open(path, 'wb').write(out)
    return len(out)


def decrire(version, w, h, parts):
    print(f'version {version}, {w}x{h}')
    iq = parts['iq']
    mis = [(o, v) for o in range(0, len(iq) - 3, 4) if (v := struct.unpack_from('<I', iq, o)[0])]
    print('  BpsIQSettings :', ','.join(f'{o},{v}' for o, v in mis))
    r = parts['regs']
    regs = struct.unpack(f'<{len(r) // 4}I', r)
    print(f'  {len(regs) // 2} registres :', ' '.join(f'{regs[i]:#x}' for i in range(0, len(regs), 2)))
    print(f'  gamma {len(parts["glut"])} o, découpage {len(parts["sl"])} o, ombrage {len(parts["lsc"])} o')
    for prog, dmi, d in luts(parts['luts']):
        print(f'  table : programme {prog}, DMI {dmi:#010x}, {len(d)} octets')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('entree')
    ap.add_argument('sortie', nargs='?')
    ap.add_argument('--iq', default='')
    ap.add_argument('--regs', default='')
    ap.add_argument('--lut', action='append', default=[])
    ap.add_argument('--sans-lut', type=int, action='append', default=[])
    ap.add_argument('--sl')
    a = ap.parse_args()

    version, w, h, parts = lire(a.entree)
    if not a.sortie:
        decrire(version, w, h, parts)
        return

    iq = bytearray(parts['iq'])
    v = [int(x, 0) for x in a.iq.split(',') if x]
    for o, val in zip(v[0::2], v[1::2]):
        if len(iq) < o + 4:
            iq += bytes(o + 4 - len(iq))
        struct.pack_into('<I', iq, o, val)
    parts['iq'] = bytes(iq)

    r = parts['regs']
    old = struct.unpack(f'<{len(r) // 4}I', r)
    regs = dict(zip(old[0::2], old[1::2]))
    v = [int(x, 0) for x in a.regs.split(',') if x]
    for ad, val in zip(v[0::2], v[1::2]):
        regs[ad] = val
    parts['regs'] = b''.join(struct.pack('<2I', ad, val) for ad, val in regs.items())

    tl = [t for t in luts(parts['luts']) if t[0] not in a.sans_lut]
    for spec in a.lut:
        prog, dmi, fichier = spec.split(',', 2)
        prog, dmi = int(prog, 0), int(dmi, 0)
        d = open(fichier, 'rb').read()
        tl = [t for t in tl if (t[0], t[1]) != (prog, dmi)] + [(prog, dmi, d)]
    parts['luts'] = b''.join(struct.pack('<3I', p, dm, len(d)) + d for p, dm, d in tl)

    if a.sl:
        parts['sl'] = open(a.sl, 'rb').read()
    n = ecrire(a.sortie, w, h, parts)
    print(f'{a.sortie} : {n} octets')
    decrire(3, w, h, parts)


if __name__ == '__main__':
    main()
