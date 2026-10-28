#!/usr/bin/python3 -I
"""Decode the register lists of a QTI CamX sensor module binary
("QTI Chromatix Header", Parameter Parser V3.4).

Layout (reverse engineered from the Xiaomi diting sensor modules):
  header: at 0xac five u32: node table offset, table size, (unused), data
  offset, data size. Node table: 56-byte entries {u32 id; char name[32];
  s32 kind[3]; u32 data_offset; u32 data_size} in schema (depth-first)
  order; data offsets are relative to the data region, equal values are
  shared.
  A regSetting array is a run of 40-byte elements:
    u32 n_slave, u32 node   slaveAddr (optional)
    u32 registerAddr
    u32 n_data,  u32 node   registerData (u32 values at the node's offset)
    u32 regAddrType, u32 regDataType, u32 operation
    u32 n_delay, u32 node   delayUs

Usage: chromatix_regs.py MODULE.bin   prints every regSetting list with the
names of the nodes before it as context. The output contains the vendor's
register settings: keep it local, do not commit it.
"""
import struct
import sys


class Module:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        tbl_off, tbl_size, _, self.data_off, _ = struct.unpack_from('<5I', self.d, 0xac)
        self.list = []
        for k in range(tbl_size // 56):
            o = tbl_off + 56 * k
            nid, = struct.unpack_from('<I', self.d, o)
            name = self.d[o + 4:o + 36].split(b'\0')[0].decode(errors='replace')
            kind = struct.unpack_from('<3i', self.d, o + 36)
            off, size = struct.unpack_from('<2I', self.d, o + 48)
            self.list.append((nid, name, kind, off, size))
        self.by_id = {n[0]: n for n in self.list}

    def values(self, nid, count):
        _, _, _, off, size = self.by_id[nid]
        n = min(count, size // 4)
        return list(struct.unpack_from(f'<{n}I', self.d, self.data_off + off))

    def decode(self, off, size):
        regs = []
        for e in range(size // 40):
            w = struct.unpack_from('<10I', self.d, self.data_off + off + 40 * e)
            n_slave, slave_node, addr, n_data, data_node, at, dt, op, n_delay, delay_node = w
            if n_data > 65536 or at > 4 or dt > 4 or op > 8:
                raise ValueError(f'bad element {e} at {off}: {w}')
            sv = self.values(slave_node, 1) if n_slave else []
            data = self.values(data_node, n_data) if n_data else []
            dv = self.values(delay_node, 1) if n_delay else []
            regs.append((sv[0] if sv else None, addr, data, at, dt, op, dv[0] if dv else 0))
        return regs


def load(path):
    return Module(path)


def main():
    m = load(sys.argv[1])
    context = []
    for nid, name, kind, off, size in m.list:
        if name == 'regSetting' and size:
            print(f'== regSetting node {nid} (after: {" / ".join(context[-3:])}) '
                  f'{size // 40} writes')
            try:
                for slave, addr, data, at, dt, op, delay in m.decode(off, size):
                    extra = f' slave=0x{slave:x}' if slave is not None else ''
                    vals = ' '.join(f'0x{v:x}' for v in data)
                    print(f'  0x{addr:04x} {vals:12s} at={at} dt={dt} op={op} '
                          f'delay={delay}us{extra}')
            except ValueError as err:
                print('  ', err)
        elif name not in ('slaveAddr', 'registerData', 'delayUs', 'regSetting') and size:
            context.append(name)


if __name__ == '__main__':
    main()
