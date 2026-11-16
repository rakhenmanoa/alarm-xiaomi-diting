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
import json, os
FUNCS = {int(k): v for k, v in json.load(open('/tmp/fw/funcs.json')).items()} if os.path.exists('/tmp/fw/funcs.json') else {}
md = capstone.Cs(capstone.CS_ARCH_XTENSA, capstone.CS_MODE_XTENSA_ESP32)
md.skipdata = True
code = read(addr, size)
pc = 0
while pc < len(code):
    ins = None if (code[pc] & 0xf) >= 14 else next(md.disasm(code[pc:pc+3], addr + pc, 1), None)
    if ins is None:
        n = 8 if (code[pc] & 0xf) >= 14 else (2 if 8 <= (code[pc] & 0xf) <= 13 else 3)
        guess = ''
        if n == 8:
            # instruction large (FLIX) : le premier emplacement ressemble à une
            # instruction de 24 bits dont le quartet op0 vaut 2 (accès mémoire)
            g = next(md.disasm(bytes([(code[pc] & 0xf0) | 2]) + code[pc+1:pc+3], addr + pc, 1), None)
            if g is not None:
                guess = f'   ; emplacement 0 ? {g.mnemonic} {g.op_str}'
        print(f'{addr+pc:#08x}: .wide    {code[pc:pc+n].hex()}{guess}' if n == 8 else f'{addr+pc:#08x}: .bytes   {code[pc:pc+n].hex()}')
        pc += n
        continue
    extra = ''
    if ins.mnemonic in ('l32r', 'call0', 'call8', 'j', 'beqz', 'bnez') or ins.mnemonic.startswith('b'):
        parts = ins.op_str.split('. ')
        if len(parts) == 2:
            try:
                rel = int(parts[1].replace(' ', ''), 0)
                base = ((addr + pc) & ~3) if ins.mnemonic in ('call0', 'call8') else addr + pc
                t = rel if ins.mnemonic == 'l32r' else base + rel
                extra = f'   ; -> {t:#x}' + (f' <{FUNCS[t]}>' if t in FUNCS else '')
                if ins.mnemonic == 'l32r':
                    d = read(t, 4)
                    if len(d) == 4:
                        extra += f' = {int.from_bytes(d, "little"):#x}'
                        v = int.from_bytes(d, 'little')
                        if v in FUNCS:
                            extra += f' <{FUNCS[v]}>'
                        else:
                            sd = read(v, 60)
                            if sd and all(32 <= c < 127 for c in sd.split(b'\0')[0][:12]) and len(sd.split(b'\0')[0]) > 3:
                                extra += ' "' + sd.split(b'\0')[0].decode()[:50] + '"'
            except ValueError:
                pass
    print(f'{ins.address:#08x}: {ins.mnemonic:8s} {ins.op_str}{extra}')
    pc += ins.size
