#!/usr/bin/env python3
"""Calcule le découpage en bandes du BPS avec la bibliothèque de CamX (libipebpsstriping.so,
arm64 Android, prise dans la sauvegarde du téléphone), exécutée dans un émulateur Unicorn.
La bibliothèque n'importe que la libc : quelques fonctions fournies ici suffisent.

Usage : bps-striping.py LIB.so LARGEUR HAUTEUR [-o sortie.bin] [--iq off,val,...]
                        [--titan 0x60800] [-v]
Sortie : BpsStripingLibOutMultiFrame au format du firmware CICP.FW.5.0 (bande de 832 octets ;
la bibliothèque en produit 848, les 748 premiers octets sont communs). Le fichier va dans
/lib/firmware et se charge avec icp_selftest_sl_file=. La bibliothèque n'est pas dans le dépôt.
Dépendances : pip install unicorn pyelftools"""
import argparse, math, re, struct, sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *

BASE, STUB, HEAP, STACK, TLS, DATA = 0x10000000, 0x20000000, 0x30000000, 0x40000000, 0x50000000, 0x60000000
MB = 1 << 20
FW_HDR, FW_STRIPE, LIB_STRIPE, LIB_COMMON = 4736, 832, 848, 748
class Emu:
    def __init__(self, path, verbose=True):
        self.verbose = verbose
        self.log = []
        f = open(path, 'rb'); self.elf = ELFFile(f); raw = open(path, 'rb').read()
        u = self.u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        top = max(s['p_vaddr'] + s['p_memsz'] for s in self.elf.iter_segments() if s['p_type'] == 'PT_LOAD')
        u.mem_map(BASE, (top + 0xffff) & ~0xffff)
        for s in self.elf.iter_segments():
            if s['p_type'] == 'PT_LOAD':
                u.mem_write(BASE + s['p_vaddr'], raw[s['p_offset']:s['p_offset'] + s['p_filesz']])
        for a, size in ((STUB, MB), (HEAP, 256 * MB), (STACK, 8 * MB), (TLS, MB), (DATA, 64 * MB)):
            u.mem_map(a, size)
        self.heap = HEAP
        u.mem_write(TLS + 0x28, struct.pack('<Q', 0x1234567890abcdef))
        u.reg_write(UC_ARM64_REG_TPIDR_EL0, TLS)
        # enable FP/SIMD
        u.reg_write(UC_ARM64_REG_CPACR_EL1, 0x300000)
        self.stubs = {}
        self.data = DATA
        dyn = self.elf.get_section_by_name('.dynamic')
        # RELR
        relr = self.elf.get_section_by_name('.relr.dyn')
        if relr:
            d = relr.data(); where = 0
            for (e,) in struct.iter_unpack('<Q', d):
                if e & 1 == 0:
                    where = e; self._rel(where); where += 8
                else:
                    bits = e >> 1
                    for i in range(63):
                        if bits & (1 << i):
                            self._rel(where + 8 * i)
                    where += 63 * 8
        # imports
        syms = self.elf.get_section_by_name('.dynsym')
        self.exports = {s.name: BASE + s['st_value'] for s in syms.iter_symbols() if s['st_shndx'] != 'SHN_UNDEF' and s.name}
        for sec in ('.rela.plt', '.rela.dyn'):
            r = self.elf.get_section_by_name(sec)
            if not r: continue
            for rel in r.iter_relocations():
                sym = syms.get_symbol(rel['r_info_sym'])
                t = rel['r_info_type']
                if t in (1026, 1025):  # JUMP_SLOT, GLOB_DAT
                    addr = self.stub(sym.name)
                    u.mem_write(BASE + rel['r_offset'], struct.pack('<Q', addr))
                elif t == 1027:  # RELATIVE
                    u.mem_write(BASE + rel['r_offset'], struct.pack('<Q', BASE + rel['r_addend']))
                elif t == 257:  # ABS64
                    addr = self.exports.get(sym.name) or self.stub(sym.name)
                    u.mem_write(BASE + rel['r_offset'], struct.pack('<Q', addr + rel['r_addend']))
        u.hook_add(UC_HOOK_CODE, self._hook, begin=STUB, end=STUB + MB)
        self.ret_addr = self.stub('__return__')

    def _rel(self, off):
        v, = struct.unpack('<Q', self.u.mem_read(BASE + off, 8))
        self.u.mem_write(BASE + off, struct.pack('<Q', v + BASE))

    def stub(self, name):
        for a, n in self.stubs.items():
            if n == name: return a
        a = STUB + 16 * len(self.stubs)
        self.stubs[a] = name
        self.u.mem_write(a, b'\xc0\x03\x5f\xd6')  # ret
        return a

    def alloc(self, n, zero=True):
        a = (self.heap + 15) & ~15
        self.heap = a + max(n, 1)
        if zero: self.u.mem_write(a, b'\0' * max(n, 1))
        return a

    def put(self, b):
        a = (self.data + 15) & ~15
        self.u.mem_write(a, bytes(b)); self.data = a + len(b) + 16
        return a

    def cstr(self, a, n=512):
        b = bytes(self.u.mem_read(a, n)); return b.split(b'\0')[0].decode('latin1')

    def x(self, i): return self.u.reg_read(UC_ARM64_REG_X0 + i) if i < 29 else None

    def fmt(self, f, args):
        out = []; i = 0; ai = 0
        import re
        for m in re.finditer(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diuxXsSpcf%]', f):
            out.append(f[i:m.start()]); i = m.end(); c = m.group()[-1]
            if c == '%': out.append('%'); continue
            v = args[ai] if ai < len(args) else 0; ai += 1
            if c == 's': out.append(self.cstr(v) if v else '(null)')
            elif c in 'di': out.append(str(v - (1 << 64) if v >> 63 else (v - (1 << 32) if 'l' not in m.group() and v >> 31 & 1 and v < 1 << 32 else v)))
            elif c in 'xX': out.append('%x' % v)
            else: out.append(str(v))
        out.append(f[i:]); return ''.join(out)

    def _hook(self, u, addr, size, _):
        name = self.stubs.get(addr)
        if name is None: return
        X = [u.reg_read(UC_ARM64_REG_X0 + i) for i in range(8)]
        ret = 0
        if name == '__return__':
            u.emu_stop(); return
        if name in ('malloc',): ret = self.alloc(X[0])
        elif name == 'calloc': ret = self.alloc(X[0] * X[1])
        elif name == 'sl_alloc':
            a = self.alloc(X[1]); u.mem_write(X[0], struct.pack('<Q', a)); ret = 0
        elif name in ('free', 'sl_free', '__cxa_finalize'): ret = 0
        elif name == 'memset':
            u.mem_write(X[0], bytes([X[1] & 0xff]) * X[2]); ret = X[0]
        elif name == 'memcpy':
            u.mem_write(X[0], bytes(u.mem_read(X[1], X[2]))); ret = X[0]
        elif name == 'memcmp':
            a = bytes(u.mem_read(X[0], X[2])); b = bytes(u.mem_read(X[1], X[2]))
            ret = 0 if a == b else (1 if a > b else (1 << 64) - 1)
        elif name == 'ldexp':
            import math
            d = u.reg_read(UC_ARM64_REG_D0); fv = struct.unpack('<d', struct.pack('<Q', d))[0]
            e = X[0] - (1 << 32) if X[0] >> 31 & 1 else X[0] & 0xffffffff
            r = math.ldexp(fv, e); u.reg_write(UC_ARM64_REG_D0, struct.unpack('<Q', struct.pack('<d', r))[0])
        elif name == '__android_log_print':
            msg = self.fmt(self.cstr(X[2]), X[3:8]); self.log.append(msg)
            if self.verbose: print('LOG:', msg)
        elif name == 'printf':
            msg = self.fmt(self.cstr(X[0]), X[1:8]); self.log.append(msg)
            if self.verbose: print('PRINTF:', msg)
        elif name == 'vsnprintf':
            s = self.cstr(X[2]).encode()[:max(X[1] - 1, 0)] + b'\0'
            if X[1]: u.mem_write(X[0], s)
            self.log.append('vsnprintf: ' + self.cstr(X[2]))
            ret = len(s) - 1
        elif name == 'fopen': ret = 0
        elif name in ('fclose', 'fprintf', 'fwrite'): ret = 0
        elif name == '__stack_chk_fail':
            raise RuntimeError('stack check failed')
        else:
            raise RuntimeError('unhandled import ' + name)
        u.reg_write(UC_ARM64_REG_X0, ret)
        u.reg_write(UC_ARM64_REG_PC, u.reg_read(UC_ARM64_REG_LR))

    def call(self, fn, *args):
        a = self.exports[fn] if isinstance(fn, str) else fn
        u = self.u
        u.reg_write(UC_ARM64_REG_SP, STACK + 8 * MB - 0x1000)
        for i, v in enumerate(args):
            u.reg_write(UC_ARM64_REG_X0 + i, v)
        u.reg_write(UC_ARM64_REG_LR, self.ret_addr)
        try:
            u.emu_start(a, self.ret_addr, timeout=0, count=0)
        except UcError as e:
            pc = u.reg_read(UC_ARM64_REG_PC)
            raise RuntimeError(f'{e} at pc {pc:#x} (lib+{pc - BASE:#x})')
        return u.reg_read(UC_ARM64_REG_X0)


def image(cfg, idx, fmt, w, h):
    o = idx * 96
    struct.pack_into('<III', cfg, o, fmt, w, h)
    if fmt == 20:		# LINEAR_NV12
        struct.pack_into('<IIII', cfg, o + 64, w, h, w, h // 2)
    else:			# Bayer 10 bits dans des mots de 16 bits
        struct.pack_into('<II', cfg, o + 64, w * 2, h)

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('lib'); ap.add_argument('width', type=int); ap.add_argument('height', type=int)
    ap.add_argument('-o', '--output', default='bps-sl.bin')
    ap.add_argument('--iq', default='492,1,664,1,780,255,788,255', help='paires octet,valeur dans BpsIQSettings')
    ap.add_argument('--titan', type=lambda v: int(v, 0), default=0x60800)
    ap.add_argument('--hw', type=lambda v: int(v, 0), default=0x20000000)
    ap.add_argument('--raw', action='store_true', help='garder les bandes de 848 octets de la bibliothèque')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()

    cfg = bytearray(1240)		# BpsConfigIoData
    image(cfg, 0, 6, a.width, a.height)	# entrée BAYER_10
    image(cfg, 1, 20, a.width, a.height)	# sortie pleine LINEAR_NV12
    iq = bytearray(1276)		# BpsIQSettings
    p = [int(v, 0) for v in a.iq.split(',') if v]
    for i in range(0, len(p) - 1, 2):
        struct.pack_into('<I', iq, p[i], p[i + 1])

    e = Emu(a.lib, verbose=a.verbose)
    pcfg, piq = e.put(cfg), e.put(iq)
    memop = e.put(struct.pack('<QQ', e.stub('sl_alloc'), e.stub('sl_free')))
    ph, pbuf, pcdm = e.put(bytes(8)), e.put(bytes(4)), e.put(bytes(4))
    rc = e.call('BPSStripingLibraryContextCreate', pcfg, memop, a.titan, a.hw, ph, pbuf, pcdm)
    if rc:
        sys.exit(f'ContextCreate : erreur {rc:#x}')
    handle, = struct.unpack('<Q', e.u.mem_read(ph, 8))
    size, = struct.unpack('<I', e.u.mem_read(pbuf, 4))
    exe = e.put(struct.pack('<QQIIQ', piq, 0, 1, 0, 0))	# BPSStripingLibExecuteParams (arm64)
    out, meta = e.alloc(size), e.alloc(284)
    user = e.put(struct.pack('<8IQI4x', 1, 0, 0, 1, 0, 0, 0, 0, 0, 0))	# userBPSArgs
    rc = e.call('BPSStripingLibraryExecute', handle, exe, out, meta, user)
    if rc:
        sys.exit(f'Execute : erreur {rc:#x}')
    lib = bytes(e.u.mem_read(out, size))

    nstripes, = struct.unpack_from('<H', lib, 0)
    if (size - FW_HDR) % LIB_STRIPE:
        sys.exit(f'taille inattendue {size} : bande de {LIB_STRIPE} octets ?')
    fw = bytearray(lib[:FW_HDR])
    stride = LIB_STRIPE if a.raw else FW_STRIPE
    for i in range(nstripes if not a.raw else 0):
        s = lib[FW_HDR + i * LIB_STRIPE:FW_HDR + (i + 1) * LIB_STRIPE]
        fw += s[:LIB_COMMON] + bytes(FW_STRIPE - LIB_COMMON)
    if a.raw:
        fw = bytearray(lib)
    open(a.output, 'wb').write(fw)

    lines = struct.unpack_from('<14H', fw, 8)
    print(f'{a.width}x{a.height} : {nstripes} bande(s), lignes entrée {lines[0]}-{lines[1]}, '
          f'sortie {lines[4]}-{lines[5]}, {len(fw)} octets dans {a.output}')
    for i in range(nstripes):
        s = FW_HDR + i * stride
        px = struct.unpack_from('<14H', fw, s)
        print(f'  bande {i} : entrée {px[0]}-{px[1]}, sortie {px[4]}-{px[5]}, '
              f'bords {fw[s + 28]},{fw[s + 29]}')

if __name__ == '__main__':
    main()
