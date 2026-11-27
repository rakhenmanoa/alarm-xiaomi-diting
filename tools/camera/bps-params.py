#!/usr/bin/env python3
"""Assemble le bloc de paramètres du pilote V4L2 du BPS (camss-bps.c, version 1).

Le pilote le lit soit dans le contrôle « BPS Parameters », soit, par défaut, dans
/lib/firmware/bps-params.bin (paramètre de module bps_params_file).

Usage : bps-params.py SORTIE.bin --taille 1920x1080 [options]
  --iq   OFF,VAL,…   octets de BpsIQSettings à mettre (comme icp_selftest_iq_patch)
  --regs REG,VAL,…   écritures de registres (comme icp_selftest_regs) ; répétable
  --cst              ajoute la conversion YUV BT.601 de CamX (18 registres à 0x5468)
  --wb   G,B,R       ajoute les gains de balance des blancs (1.0 = neutre)
  --gamma FICHIER    tables gamma de bps-gamma.py (3 × 1 Ko)
  --sl   FICHIER     découpage de bps-striping.py (même taille et mêmes --iq)

Exemple (chaîne du 23 nov. 2026) :
  bps-params.py bps-params.bin --taille 1920x1080 \\
      --iq 492,1,652,1,660,1,664,1,780,255,788,255,988,1,992,3 --cst --wb 1,1.415,1.513 \\
      --regs $(bps-ccm.py 1 0 0 0 1 0 0 0 1) --gamma gamma.bin --sl sl-1920x1080.bin"""
import argparse
import struct

MAGIC, VERSION = 0x50535042, 1
IQ_SIZE, GLUT_SIZE, SL_MAX, REGS_MAX = 1276, 0xc00, 0x4000, (0x1000 - 4) // 8
TOTAL_MAX = 32768

CST_601 = [
    0x5468, 0x750259, 0x546c, 0x132, 0x5470, 0x0, 0x5474, 0x0, 0x5478, 0x0,
    0x547c, 0xfff00000, 0x5480, 0x1fe1eae, 0x5484, 0x1f54, 0x5488, 0x40000000,
    0x548c, 0x0, 0x5490, 0x0, 0x5494, 0xfff00000, 0x5498, 0x1fad1e55, 0x549c, 0x1fe,
    0x54a0, 0x40000000, 0x54a4, 0x0, 0x54a8, 0x0, 0x54ac, 0xfff00000,
]


def ints(text):
    return [int(v, 0) for v in text.replace(' ', '').split(',') if v]


p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
p.add_argument('sortie')
p.add_argument('--taille', required=True)
p.add_argument('--iq', default='')
p.add_argument('--regs', action='append', default=[])
p.add_argument('--cst', action='store_true')
p.add_argument('--wb')
p.add_argument('--gamma')
p.add_argument('--sl')
a = p.parse_args()

w, h = (int(v) for v in a.taille.lower().split('x'))

iq = bytearray(IQ_SIZE)
pairs = ints(a.iq)
for off, val in zip(pairs[0::2], pairs[1::2]):
    if off % 4 or off + 4 > IQ_SIZE:
        raise SystemExit(f'--iq : décalage {off} invalide')
    struct.pack_into('<I', iq, off, val & 0xffffffff)

regs = []
if a.cst:
    regs += CST_601
if a.wb:
    gains = [float(v) for v in a.wb.split(',')]
    if len(gains) != 3:
        raise SystemExit('--wb : trois gains G,B,R')
    wb = [min(0x7fff, round(g * 1024)) << 17 for g in gains] + [0] * 9
    for i, v in enumerate(wb):
        regs += [0x3868 + 4 * i, v]
for r in a.regs:
    regs += ints(r)
if len(regs) % 2 or len(regs) // 2 > REGS_MAX:
    raise SystemExit(f'--regs : paires (registre, valeur), {REGS_MAX} au plus')
regs = struct.pack(f'<{len(regs)}I', *(v & 0xffffffff for v in regs))

glut = open(a.gamma, 'rb').read() if a.gamma else b''
if glut and len(glut) != GLUT_SIZE:
    raise SystemExit(f'--gamma : {GLUT_SIZE} octets attendus')
sl = open(a.sl, 'rb').read() if a.sl else b''
if len(sl) > SL_MAX:
    raise SystemExit(f'--sl : {SL_MAX} octets au plus')

HEADER = 48
sections, data, off = [], b'', HEADER
for blob in (bytes(iq), regs, glut, sl):
    sections += [off if blob else 0, len(blob)]
    data += blob
    off += len(blob)
    # chaque section commence sur 4 octets
    pad = -off % 4
    data += bytes(pad)
    off += pad

out = struct.pack('<4I8I', MAGIC, VERSION, w, h, *sections) + data
if len(out) > TOTAL_MAX:
    raise SystemExit(f'bloc de {len(out)} octets, {TOTAL_MAX} au plus')
open(a.sortie, 'wb').write(out)
print(f'{a.sortie} : {len(out)} octets, {w}x{h}, {len(regs) // 8} registres, '
      f'gamma {"oui" if glut else "non"}, découpage {len(sl)} octets'
      + ('' if sl else ' (ATTENTION : sans découpage, le BPS ne traite rien)'))
