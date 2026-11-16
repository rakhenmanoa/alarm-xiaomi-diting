#!/usr/bin/env python3
"""Décode les commandes CDM affichées par l'autotest ICP (lignes « selftest: cdm »).
Usage : dmesg | tools/camera/cdm-decode.py"""
import re, sys

words = {}
for line in sys.stdin:
    m = re.search(r'selftest: cdm ([0-9a-f]+): ((?:[0-9a-f]{8} ?){4})', line)
    if m:
        off = int(m.group(1), 16)
        for i, w in enumerate(m.group(2).split()):
            words[off + 4 * i] = int(w, 16)

NAMES = {3: 'REG_CONT', 4: 'REG_RANDOM', 5: 'BUFF_INDIRECT', 6: 'GEN_IRQ',
         7: 'WAIT_EVENT', 8: 'CHANGE_BASE', 0xa: 'DMI_32', 0xb: 'DMI_64'}
pos = 0
end = max(words) + 4 if words else 0
while pos < end:
    w = words.get(pos, 0)
    if w == 0:
        pos += 4
        continue
    op, cnt = w >> 24, w & 0xffff
    name = NAMES.get(op, f'op{op:#x}')
    if op == 4:
        print(f'{pos:05x} {name} x{cnt}')
        for i in range(cnt):
            r, v = words.get(pos + 4 + 8 * i, 0), words.get(pos + 8 + 8 * i, 0)
            print(f'        {r:#07x} <- {v:#x}')
        pos += 4 + 8 * cnt
    elif op == 3:
        base = words.get(pos + 4, 0)
        print(f'{pos:05x} {name} x{cnt} from {base:#07x}')
        for i in range(cnt):
            print(f'        {base + 4 * i:#07x} <- {words.get(pos + 8 + 4 * i, 0):#x}')
        pos += 8 + 4 * cnt
    else:
        print(f'{pos:05x} {name} {w:#010x}')
        pos += 4
