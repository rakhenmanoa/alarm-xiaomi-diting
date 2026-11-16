#!/usr/bin/env python3
"""Affiche les énumérations DWARF qui contiennent un énumérateur donné."""
import sys
from elftools.elf.elffile import ELFFile
ef = ELFFile(open(sys.argv[1], 'rb'))
want = set(sys.argv[2:])
done = set()
for cu in ef.get_dwarf_info().iter_CUs():
    for die in cu.iter_DIEs():
        if die.tag != 'DW_TAG_enumeration_type':
            continue
        items = [(c.attributes['DW_AT_name'].value.decode(), c.attributes['DW_AT_const_value'].value)
                 for c in die.iter_children() if c.tag == 'DW_TAG_enumerator']
        hit = want & {n for n, _ in items}
        if hit and not hit <= done:
            done |= hit
            n = die.attributes.get('DW_AT_name')
            print('===', n.value.decode() if n else '(anonyme)')
            for name, v in items:
                print(f'  {v:4d} {name}')
    if done == want:
        break
