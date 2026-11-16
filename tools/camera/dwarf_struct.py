#!/usr/bin/env python3
"""Affiche la disposition de structures/énumérations lues dans le DWARF d'un ELF.
Usage : dwarf_struct.py fichier.elf Nom [Nom...]   (--depth N pour développer)"""
import sys
from elftools.elf.elffile import ELFFile

def main():
    args = sys.argv[1:]
    depth = 1
    if '--depth' in args:
        i = args.index('--depth'); depth = int(args[i+1]); del args[i:i+2]
    path, names = args[0], set(args[1:])
    ef = ELFFile(open(path, 'rb'))
    dw = ef.get_dwarf_info()
    found = {}
    for cu in dw.iter_CUs():
        for die in cu.iter_DIEs():
            if die.tag in ('DW_TAG_structure_type', 'DW_TAG_union_type', 'DW_TAG_enumeration_type', 'DW_TAG_typedef'):
                n = die.attributes.get('DW_AT_name')
                if n and n.value.decode() in names and n.value.decode() not in found:
                    if 'DW_AT_declaration' in die.attributes:
                        continue
                    if die.tag == 'DW_TAG_typedef':
                        t = resolve(die)
                        if t is None or t.tag == 'DW_TAG_typedef' or 'DW_AT_declaration' in t.attributes:
                            continue
                        die = t
                    found[n.value.decode()] = die
        if len(found) == len(names):
            break
    for n in sys.argv[1:]:
        if n in found:
            print(f"=== {n}")
            dump(found[n], 0, 0, depth)
    for n in names - set(found):
        print(f"=== {n} : introuvable")

def ref(die, attr='DW_AT_type'):
    a = die.attributes.get(attr)
    return die.get_DIE_from_attribute(attr) if a else None

def resolve(die):
    t = ref(die)
    while t is not None and t.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type'):
        t = ref(t)
    return t

def tname(die):
    if die is None:
        return 'void'
    n = die.attributes.get('DW_AT_name')
    if die.tag == 'DW_TAG_pointer_type':
        return tname(ref(die)) + ' *'
    if die.tag == 'DW_TAG_array_type':
        dims = ''.join('[%d]' % (c.attributes['DW_AT_upper_bound'].value + 1) if 'DW_AT_upper_bound' in c.attributes else '[]'
                       for c in die.iter_children() if c.tag == 'DW_TAG_subrange_type')
        return tname(ref(die)) + dims
    if n:
        return n.value.decode()
    if die.tag in ('DW_TAG_const_type', 'DW_TAG_volatile_type'):
        return tname(ref(die))
    return die.tag.replace('DW_TAG_', '<') + '>'

def size(die):
    t = die
    while t is not None and 'DW_AT_byte_size' not in t.attributes:
        if t.tag == 'DW_TAG_array_type':
            n = 1
            for c in t.iter_children():
                if c.tag == 'DW_TAG_subrange_type' and 'DW_AT_upper_bound' in c.attributes:
                    n *= c.attributes['DW_AT_upper_bound'].value + 1
            s = size(ref(t))
            return n * s if s else None
        t = ref(t)
    return t.attributes['DW_AT_byte_size'].value if t is not None else None

def off(m):
    a = m.attributes.get('DW_AT_data_member_location')
    if a is None:
        return 0
    v = a.value
    if isinstance(v, list):
        # DW_OP_plus_uconst (0x23) + uleb
        r, s = 0, 0
        for b in v[1:]:
            r |= (b & 0x7f) << s; s += 7
        return r
    return v

def dump(die, base, ind, depth):
    pad = '  ' * ind
    if die.tag == 'DW_TAG_enumeration_type':
        for c in die.iter_children():
            if c.tag == 'DW_TAG_enumerator':
                print(f"{pad}  {c.attributes['DW_AT_const_value'].value:4d} {c.attributes['DW_AT_name'].value.decode()}")
        return
    print(f"{pad}  (taille {size(die)})")
    for m in die.iter_children():
        if m.tag != 'DW_TAG_member':
            continue
        t = ref(m)
        o = base + off(m)
        bits = ''
        if 'DW_AT_bit_size' in m.attributes:
            bits = f" :{m.attributes['DW_AT_bit_size'].value}"
        print(f"{pad}  +{o:5d} ({size(t)!s:>5}) {tname(t)} {m.attributes['DW_AT_name'].value.decode()}{bits}")
        r = t
        while r is not None and r.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_array_type'):
            r = ref(r)
        if depth > 1 and r is not None and r.tag in ('DW_TAG_structure_type', 'DW_TAG_union_type'):
            dump(r, o, ind + 1, depth - 1)

main()
