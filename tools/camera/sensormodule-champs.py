#!/usr/bin/python3 -I
"""Affiche les champs nommés d'un module capteur CamX (com.qti.sensormodule.*.bin) :
description des modes (resolutionData), contrôle de l'exposition, capacités…,
sans les listes de registres (voir chromatix_regs.py / chromatix-to-camregs.py).

  sensormodule-champs.py MODULE.bin > champs.txt

Chaque ligne : profondeur (indentation), nom du nœud, nombre d'éléments, puis les
valeurs (entiers ; entre crochets la lecture en flottant quand elle est plausible).
Les nœuds de registres (registerAddr, registerData, delayUs…) et les grosses
tables sont résumés. La sortie contient des réglages du fabricant : la garder en
local, ne pas la mettre dans le dépôt.
"""
import importlib.util
import os
import struct
import sys

spec = importlib.util.spec_from_file_location(
    'chromatix_regs', os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   'chromatix_regs.py'))
cr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cr)

REGS = {'registerAddr', 'registerData', 'delayUs', 'slaveAddr', 'regAddrType',
        'regDataType', 'operation'}


def flottant(v):
    f, = struct.unpack('<f', struct.pack('<I', v))
    if v and 1e-4 < abs(f) < 1e7 and v not in range(0, 0x100000):
        return f'{f:g}'
    return None


def main():
    m = cr.Module(sys.argv[1])
    resume = {}
    for nid, name, kind, off, size in m.list:
        depth = 0
        if name in REGS:
            resume[name] = resume.get(name, 0) + 1
            continue
        n = size // 4
        pre = '  ' * min(depth, 8)
        if n == 0:
            print(f'{pre}{name}')
            continue
        if n > 24:
            print(f'{pre}{name} [{n} valeurs, table résumée]')
            continue
        vals = struct.unpack_from(f'<{n}I', m.d, m.data_off + off)
        txt = []
        for v in vals:
            f = flottant(v)
            txt.append(f'{v}' + (f' [{f}]' if f else '') if v < 0x10000 or f else f'{v:#x}')
        print(f'{pre}{name} = {" ".join(txt)}   (nœud {nid}, kind {kind})')
    print('nœuds de registres résumés :', resume, file=sys.stderr)


if __name__ == '__main__':
    main()
