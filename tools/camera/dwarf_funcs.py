#!/usr/bin/env python3
"""Liste les fonctions (nom, adresse, taille) des unités DWARF dont le nom contient un motif."""
import sys
from elftools.elf.elffile import ELFFile
ef = ELFFile(open(sys.argv[1], 'rb'))
for cu in ef.get_dwarf_info().iter_CUs():
    top = cu.get_top_DIE()
    name = top.attributes.get('DW_AT_name')
    name = name.value.decode() if name else ''
    if not any(p.lower() in name.lower() for p in sys.argv[2:]):
        continue
    print('##', name)
    for die in top.iter_children():
        if die.tag == 'DW_TAG_subprogram' and 'DW_AT_low_pc' in die.attributes:
            lo = die.attributes['DW_AT_low_pc'].value
            hi = die.attributes['DW_AT_high_pc'].value
            if die.attributes['DW_AT_high_pc'].form != 'DW_FORM_addr':
                hi += lo
            print(f"  {lo:#08x} {hi-lo:5d} {die.attributes['DW_AT_name'].value.decode()}")
