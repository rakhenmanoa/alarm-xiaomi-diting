#!/usr/bin/python3 -I
"""Affiche les métadonnées d'un module capteur CamX (com.qti.sensormodule.*.bin)
utiles pour écrire un pilote : adresse I2C, identifiant de puce, alimentation,
CSI (lignes, débit), horloges et timings des modes. Les tables de registres
ne sont pas affichées, seulement leur taille (elles sont converties en
« camregs » par chromatix-to-camregs.py de diting-ubuntu).

  camx-infos.py MODULE.bin [> infos.txt]

Format du fichier (rétro-ingénierie de royka1, tools/camera/chromatix_regs.py
de diting-ubuntu) : à 0xac, cinq u32 (table des nœuds, taille, -, données,
taille). Nœuds de 56 octets {u32 id; char nom[32]; s32 kind[3];
u32 décalage; u32 taille} dans l'ordre du schéma (parcours en profondeur).
Les valeurs sont affichées en u32, et aussi en float quand c'est plausible :
le type exact n'est pas connu, à interpréter selon le nom.
"""
import math
import struct
import sys


def charger(path):
    d = open(path, 'rb').read()
    tbl_off, tbl_size, _, data_off, _ = struct.unpack_from('<5I', d, 0xac)
    nodes = []
    for k in range(tbl_size // 56):
        o = tbl_off + 56 * k
        nid, = struct.unpack_from('<I', d, o)
        name = d[o + 4:o + 36].split(b'\0')[0].decode(errors='replace')
        kind = struct.unpack_from('<3i', d, o + 36)
        off, size = struct.unpack_from('<2I', d, o + 48)
        nodes.append((nid, name, kind, off, size))
    return d, data_off, nodes


def flottant_plausible(u):
    f = struct.unpack('<f', struct.pack('<I', u))[0]
    if u == 0 or math.isnan(f) or math.isinf(f):
        return None
    a = abs(f)
    if 1e-6 <= a <= 1e7 and u > 0x10000:
        return f
    return None


def apercu(d, data_off, off, size):
    raw = d[data_off + off:data_off + off + size]
    # chaîne ASCII terminée par 0
    s = raw.split(b'\0')[0]
    if len(s) >= 3 and all(32 <= c < 127 for c in s) and len(s) >= len(raw.rstrip(b'\0')) - 1:
        return repr(s.decode())
    vals = struct.unpack_from(f'<{size // 4}I', raw) if size >= 4 else ()
    parts = []
    for u in vals[:16]:
        f = flottant_plausible(u)
        parts.append(f'{u}' + (f' (~{f:g})' if f is not None else '') if u < 0x10000 or f is None
                     else f'0x{u:x} (~{f:g})')
    if len(vals) > 16:
        parts.append(f'… {len(vals)} valeurs')
    return ' '.join(parts) if parts else raw.hex()


def main():
    d, data_off, nodes = charger(sys.argv[1])
    profondeur = []
    for nid, name, kind, off, size in nodes:
        if name == 'regSetting':
            print(f'  [regSetting : {size // 40} écritures]')
            continue
        if name in ('slaveAddr', 'registerData', 'delayUs'):
            continue
        valeur = apercu(d, data_off, off, size) if 0 < size <= 256 else (f'<{size} octets>' if size else '')
        print(f'{nid:5d} {name:32s} kind={kind[0]},{kind[1]},{kind[2]} {valeur}')


if __name__ == '__main__':
    main()
