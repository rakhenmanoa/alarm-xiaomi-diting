#!/usr/bin/env python3
"""Affiche les paramètres (nom, type) de fonctions du firmware. Usage : params.py elf Fonction..."""
import sys
from elftools.elf.elffile import ELFFile
ef = ELFFile(open(sys.argv[1], 'rb')); want = set(sys.argv[2:])
def tname(die):
    if die is None: return 'void'
    n = die.attributes.get('DW_AT_name')
    t = die.get_DIE_from_attribute('DW_AT_type') if 'DW_AT_type' in die.attributes else None
    if die.tag == 'DW_TAG_pointer_type': return tname(t) + ' *'
    if die.tag in ('DW_TAG_const_type', 'DW_TAG_volatile_type'): return 'const ' + tname(t)
    return n.value.decode() if n else die.tag
for cu in ef.get_dwarf_info().iter_CUs():
    for die in cu.get_top_DIE().iter_children():
        if die.tag == 'DW_TAG_subprogram' and 'DW_AT_name' in die.attributes and die.attributes['DW_AT_name'].value.decode() in want and 'DW_AT_low_pc' in die.attributes:
            print('==', die.attributes['DW_AT_name'].value.decode())
            for c in die.iter_children():
                if c.tag == 'DW_TAG_formal_parameter':
                    t = c.get_DIE_from_attribute('DW_AT_type') if 'DW_AT_type' in c.attributes else None
                    print('   ', tname(t), c.attributes['DW_AT_name'].value.decode() if 'DW_AT_name' in c.attributes else '?')
