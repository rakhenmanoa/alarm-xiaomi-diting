#!/usr/bin/python3 -I
"""Matrices de couleurs (module CC 1.3) d'un fichier de réglage CamX
(com.qti.tuned.*.bin, « QTI Chromatix Header », Parameter Parser V3.4) et leur
interpolation selon l'éclairage, comme le fait CamX.

  chromatix-cc13.py TUNED.bin --liste
      instances du module (cc13_bps_v2, cc13_ipe_v2…) et leurs groupes identiques
  chromatix-cc13.py TUNED.bin --table [--instance N]
      arbre de déclencheurs : plages d'indice de luminosité et de température de
      couleur, matrice de chaque région
  chromatix-cc13.py TUNED.bin --lux 300 --cct 4000 [--instance N] [-v]
      matrice interpolée, 9 nombres en ordre RVB (à passer à bps-ccm.py)

Format (rétro-ingénierie du fichier du HP1, 27 nov. 2026) :
- à 0xac, cinq u32 : table des nœuds, taille, -, zone de données, taille ;
  nœuds de 56 octets {u32 id; char nom[32]; s32 kind[3]; u32 décalage; u32 taille},
  décalages relatifs à la zone de données.
- Les modules sont les nœuds dont kind[1] = 1 (cc13_bps_v2, cc13_ipe_v2…) ;
  kind[2] numérote l'instance (1 = BPS par défaut, 2 = IFE, 4 = IPE par défaut,
  les autres sont des variantes par cas d'usage). En-tête de 52 octets : le dernier
  u32 est l'id du nœud racine mod_cc13_trigger_data.
- Un nœud de déclencheurs est un tableau d'entrées de 24 octets
  {float début, fin; u32 n_enfants, id_enfants; u32 n_régions, id_région}.
  id_enfants est le nœud qui contient le tableau des n entrées du niveau suivant.
- Niveaux du CC 1.3 : gain DRC [1-100], rapport HDR [1-16 ou 64], indice LED [0],
  indice de luminosité AEC (lux index : petit = lumineux), température de couleur (K).
- Région : 12 flottants, c_tab[9] (matrice 3x3, lignes = sorties R, V, B ; chaque
  ligne fait 1) puis k_tab[3] (décalages, nuls dans ce fichier).
- Interpolation (comme IQInterpolation de CamX) : dans [début, fin] d'une région,
  sa valeur ; entre la fin d'une région et le début de la suivante, mélange
  linéaire des deux ; hors des plages, la région la plus proche.

La sortie contient des réglages du fabricant : la garder en local, ne pas la
mettre dans le dépôt.
"""
import argparse
import struct
import sys


class Tuned:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        tbl, size, _, self.data, _ = struct.unpack_from('<5I', self.d, 0xac)
        self.nodes = {}
        self.order = []
        for k in range(size // 56):
            o = tbl + 56 * k
            nid, = struct.unpack_from('<I', self.d, o)
            name = self.d[o + 4:o + 36].split(b'\0')[0].decode(errors='replace')
            kind = struct.unpack_from('<3i', self.d, o + 36)
            off, sz = struct.unpack_from('<2I', self.d, o + 48)
            self.nodes[nid] = (name, kind, off, sz)
            self.order.append(nid)

    def raw(self, nid):
        _, _, off, sz = self.nodes[nid]
        return self.d[self.data + off:self.data + off + sz]

    def modules(self, prefix):
        for nid in self.order:
            name, kind, off, sz = self.nodes[nid]
            if name.startswith(prefix) and kind[1] == 1:
                yield name, kind[2], struct.unpack_from('<13I', self.raw(nid))[12]

    def entries(self, nid):
        b = self.raw(nid)
        return [struct.unpack_from('<2f4I', b, 24 * j) for j in range(len(b) // 24)]

    def region(self, nid):
        b = self.raw(nid)
        return list(struct.unpack('<%df' % (len(b) // 4), b))

    def leaves(self, nid, path=()):
        for s, e, nc, cid, nr, rid in self.entries(nid):
            p = path + ((s, e),)
            if nc:
                yield from self.leaves(cid, p)
            if nr:
                yield p, self.region(rid)

    def depth(self, nid):
        s, e, nc, cid, nr, rid = self.entries(nid)[0]
        return 1 + (self.depth(cid) if nc else 0)

    def interp(self, nid, values, trace=None, level=0):
        """Moyenne pondérée des régions pour les valeurs de déclencheur données."""
        ent = self.entries(nid)
        v = values[level] if level < len(values) else ent[0][0]
        picks = weights(ent, v)
        out = None
        for i, w in picks:
            s, e, nc, cid, nr, rid = ent[i]
            if trace is not None:
                trace.append(f'{"  " * level}niveau {level} = {v:g} : [{s:g}, {e:g}] poids {w:.3f}')
            val = self.interp(cid, values, trace, level + 1) if nc else self.region(rid)
            out = [w * x for x in val] if out is None else [o + w * x for o, x in zip(out, val)]
        return out


def weights(ent, v):
    if v <= ent[0][1]:
        return [(0, 1.0)]
    for i, (s, e, *_) in enumerate(ent):
        if s <= v <= e:
            return [(i, 1.0)]
        if i + 1 < len(ent):
            s2 = ent[i + 1][0]
            if e < v < s2:
                t = (v - e) / (s2 - e)
                return [(i, 1.0 - t), (i + 1, t)]
    return [(len(ent) - 1, 1.0)]


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('tuned')
    p.add_argument('--module', default='cc13_', help='préfixe du nom de module (cc13_)')
    p.add_argument('--instance', type=int, default=929,
                   help='kind[2] de l\'instance (929 : BPS, mêmes matrices que l\'IPE par défaut)')
    p.add_argument('--liste', action='store_true')
    p.add_argument('--table', action='store_true')
    p.add_argument('--drc', type=float, default=1.0)
    p.add_argument('--hdr', type=float, default=1.0)
    p.add_argument('--led', type=float, default=0.0)
    p.add_argument('--lux', type=float, help='indice de luminosité AEC (petit = lumineux)')
    p.add_argument('--cct', type=float, help='température de couleur en kelvins')
    p.add_argument('-v', action='store_true', help='détail de l\'interpolation sur stderr')
    a = p.parse_args()

    t = Tuned(a.tuned)
    mods = list(t.modules(a.module))
    if a.liste:
        groupes = {}
        for name, inst, root in mods:
            cle = repr(list(t.leaves(root)))
            groupes.setdefault(cle, []).append(f'{name}:{inst}')
        for k, (cle, noms) in enumerate(groupes.items()):
            n = cle.count('), [')
            print(f'groupe {k} ({len(noms)} instances, {n + 1} régions) : {" ".join(noms)}')
        return
    roots = [r for name, inst, r in mods if inst == a.instance]
    if not roots:
        sys.exit(f'instance {a.instance} introuvable (voir --liste)')
    root = roots[0]
    if a.table:
        for path, v in t.leaves(root):
            plages = ' '.join(f'[{s:g}-{e:g}]' for s, e in path)
            print(plages, ' '.join(f'{x:+.4f}' for x in v[:9]),
                  '| k', ' '.join(f'{x:g}' for x in v[9:12]))
        return
    if a.lux is None or a.cct is None:
        sys.exit('--lux et --cct sont nécessaires (ou --liste, --table)')
    n = t.depth(root)
    vals = [a.drc, a.hdr, a.led, a.lux, a.cct][-n:] if n <= 5 else None
    if vals is None:
        sys.exit(f'arbre à {n} niveaux, non prévu')
    trace = [] if a.v else None
    m = t.interp(root, vals, trace)
    if trace:
        print('\n'.join(trace), file=sys.stderr)
        for r in range(3):
            print('  ligne %d : %s (somme %.4f)' % (r, ' '.join(f'{x:+.4f}' for x in m[3 * r:3 * r + 3]),
                                                   sum(m[3 * r:3 * r + 3])), file=sys.stderr)
    print(' '.join(f'{x:.4f}' for x in m[:9]))


if __name__ == '__main__':
    main()
