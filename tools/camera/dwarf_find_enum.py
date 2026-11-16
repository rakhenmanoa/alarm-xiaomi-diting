#!/usr/bin/env python3
"""Liste les énumérations DWARF dont un énumérateur contient un motif."""
import re, sys
from elftools.elf.elffile import ELFFile
ef = ELFFile(open(sys.argv[1], 'rb'))
pat = re.compile(sys.argv[2])
seen = set()
for cu in ef.get_dwarf_info().iter_CUs():
    for die in cu.iter_DIEs():
        if die.tag != 'DW_TAG_enumeration_type':
            continue
        items = tuple((c.attributes['DW_AT_name'].value.decode(), c.attributes['DW_AT_const_value'].value)
                      for c in die.iter_children() if c.tag == 'DW_TAG_enumerator')
        if items in seen or not any(pat.search(n) for n, _ in items):
            continue
        seen.add(items)
        n = die.attributes.get('DW_AT_name')
        print('===', n.value.decode() if n else '(anonyme)', len(items))
        for name, v in items:
            print(f'  {v:4d} {name}')
