#!/usr/bin/python3 -I
"""Copie de tools/camera/chromatix-to-camregs.py de diting-ubuntu (royka1,
GPL-2.0), étendue aux adresses de registre 8 bits (regAddrType 1, GC02M1) :
elles sont stockées telles quelles dans le champ u16, le pilote du capteur
sait que ses registres ont des adresses 8 bits.

Convert the register lists of a QTI CamX sensor module binary into a
"camregs" firmware file for the diting sensor drivers.

  chromatix-to-camregs.py MODULE.bin OUT.camregs

camregs format (little endian):
  char magic[8] = "CAMREGS1" or "CAMREGS2"; u32 n_tables;
  n_tables x { char name[16]; u32 n_entries;
               n_entries x { u16 addr; u16 value; u8 width; u8 pad;
                             u16 delay_us } }
  width 1 or 2: a register write of value. CAMREGS2 only: width 0 is a
  burst of `value` bytes to consecutive addresses, the bytes follow the
  entry, padded to a multiple of 4. Version 2 is written only when a table
  has bursts.

Tables: "init" (initSettings) and "mode0".."modeN" (resolutionData, in
module order). Only plain register writes are converted; the stream on/off
and group hold lists are not needed (the drivers write those registers).
The output contains the vendor's register settings: it is built into the
local firmware package and never committed.
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

module, out = sys.argv[1], sys.argv[2]
tables = []
nodes = cr.load(module)
context = []
n_modes = 0
# Une table de mode est la première liste de registres qui suit la
# streamConfiguration de ce mode (d'autres nœuds peuvent s'intercaler,
# downScaleFactor ou integrationInfo selon la version de CamX).
mode_pending = False
for nid, name, kind, off, size in nodes.list:
    if name == 'regSetting' and size:
        if context and context[-1] == 'initSettings':
            label = 'init'
        elif mode_pending or (context and context[-1] in ('vc', 'streamConfiguration')):
            label = f'mode{n_modes}'
            n_modes += 1
            mode_pending = False
        else:
            label = None
        if label:
            entries = []
            for slave, addr, data, at, dt, op, delay in nodes.decode(off, size):
                # op 0: write, op 2: sequential (burst) write
                # at 1 : adresses 8 bits (GalaxyCore GC02M1), at 2 : 16 bits
                if op not in (0, 2) or at not in (1, 2) or dt not in (1, 2) or slave is not None \
                        or (at == 1 and addr > 0xff):
                    sys.exit(f'{label}: unsupported write at 0x{addr:04x} '
                             f'(op={op} at={at} dt={dt} slave={slave})')
                if len(data) > 1:               # burst: consecutive registers
                    raw = b''.join(v.to_bytes(dt, 'big') for v in data)
                    entries.append((addr, raw, 0, delay))
                else:
                    entries.append((addr, data[0], dt, delay))
            tables.append((label, entries))
        context.append('regSetting')
    elif name not in ('slaveAddr', 'registerData', 'delayUs') and size:
        context.append(name)
        if name == 'streamConfiguration':
            mode_pending = True

if not any(t[0] == 'init' for t in tables) or not n_modes:
    sys.exit('no init or mode tables found')

bursts = any(w == 0 for _, entries in tables for _, _, w, _ in entries)
blob = bytearray((b'CAMREGS2' if bursts else b'CAMREGS1') + struct.pack('<I', len(tables)))
for label, entries in tables:
    blob += label.encode().ljust(16, b'\0') + struct.pack('<I', len(entries))
    for addr, value, width, delay in entries:
        if delay > 0xffff:
            sys.exit(f'{label}: delay out of range at 0x{addr:04x}')
        if width == 0:
            if len(value) > 0xffff:
                sys.exit(f'{label}: burst too long at 0x{addr:04x}')
            blob += struct.pack('<HHBBH', addr, len(value), 0, 0, delay)
            blob += value + b'\0' * (-len(value) % 4)
        else:
            if value > 0xffff:
                sys.exit(f'{label}: value out of range at 0x{addr:04x}')
            blob += struct.pack('<HHBBH', addr, value, width, 0, delay)
with open(out, 'wb') as f:
    f.write(blob)
for label, entries in tables:
    print(f'{label}: {len(entries)} entries, '
          f'{sum(len(v) for _, v, w, _ in entries if w == 0)} burst bytes')
