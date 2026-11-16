#!/usr/bin/env python3
"""Désassemble une plage d'adresses du firmware Xtensa. Usage : dis.py elf adresse taille"""
import sys, capstone
from elftools.elf.elffile import ELFFile
ef = ELFFile(open(sys.argv[1], 'rb'))
addr, size = int(sys.argv[2], 16), int(sys.argv[3], 0)
def read(a, n):
    for s in ef.iter_sections():
        if s['sh_type'] == 'SHT_PROGBITS' and s['sh_addr'] <= a < s['sh_addr'] + s['sh_size'] and s['sh_flags'] & 2:
            return s.data()[a - s['sh_addr']: a - s['sh_addr'] + n]
    return b''
md = capstone.Cs(capstone.CS_ARCH_XTENSA, capstone.CS_MODE_XTENSA_ESP32)
md.skipdata = True
code = read(addr, size)
pc = 0
while pc < len(code):
    ins = None if (code[pc] & 0xf) >= 14 else next(md.disasm(code[pc:pc+3], addr + pc, 1), None)
    if ins is None:
        n = 8 if (code[pc] & 0xf) >= 14 else (2 if 8 <= (code[pc] & 0xf) <= 13 else 3)
        print(f'{addr+pc:#08x}: .bytes   {code[pc:pc+n].hex()}')
        pc += n
        continue
    extra = ''
    if ins.mnemonic in ('l32r', 'call0', 'call8', 'j', 'beqz', 'bnez') or ins.mnemonic.startswith('b'):
        parts = ins.op_str.split('. ')
        if len(parts) == 2:
            try:
                rel = int(parts[1].replace(' ', ''), 0)
                base = ((addr + pc + 3) & ~3) if ins.mnemonic in ('l32r', 'call0', 'call8') else addr + pc
                t = base + rel
                extra = f'   ; -> {t:#x}'
                if ins.mnemonic == 'l32r':
                    d = read(t, 4)
                    if len(d) == 4:
                        extra += f' = {int.from_bytes(d, "little"):#x}'
            except ValueError:
                pass
    print(f'{ins.address:#08x}: {ins.mnemonic:8s} {ins.op_str}{extra}')
    pc += ins.size
