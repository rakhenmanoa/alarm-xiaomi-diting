#!/usr/bin/python3 -I
"""Cherche des nœuds dans un dtbo.img d'Android (en-tête 0xd7b7ab1e, une entrée DTB par
variante de carte) : chaque entrée est décompilée avec dtc et les nœuds dont le nom ou une
propriété contient le motif sont affichés en entier, avec leur chemin.

  dtbo-noeuds.py dtbo.img [MOTIF=ois] [--entree N] > noeuds.txt

Les phandles restent numériques (dtc ne connaît pas l'arbre de base) : chercher les nœuds
référencés avec un second appel (par exemple MOTIF=phandle\\ =\\ <0x1f3>).
Demande dtc (paquet dtc).
"""
import re
import struct
import subprocess
import sys


def entrees(b):
    magic, total, hdr, esz, n, off = struct.unpack_from('>6I', b, 0)
    if magic != 0xd7b7ab1e:
        sys.exit('pas un dtbo.img (en-tête 0xd7b7ab1e attendu)')
    for i in range(n):
        size, o = struct.unpack_from('>2I', b, off + i * esz)
        yield i, b[o:o + size]


def noeuds(dts, motif):
    """Blocs { … } dont la ligne d'ouverture ou une propriété directe contient le motif."""
    lignes = dts.splitlines()
    pile, sorties = [], []
    for k, l in enumerate(lignes):
        s = l.strip()
        if s.endswith('{'):
            pile.append([k, s[:-1].strip(), False])
            if re.search(motif, s, re.I):
                pile[-1][2] = True
        elif s.startswith('};'):
            if not pile:
                continue
            debut, nom, garde = pile.pop()
            if garde:
                chemin = '/'.join(p[1] for p in pile) + '/' + nom
                sorties.append((chemin, '\n'.join(lignes[debut:k + 1])))
        elif pile and re.search(motif, s, re.I):
            pile[-1][2] = True
    return sorties


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    seule = None
    if '--entree' in sys.argv:
        seule = int(sys.argv[sys.argv.index('--entree') + 1])
        args = [a for a in args if a != str(seule)]
    if not args:
        sys.exit(__doc__)
    b = open(args[0], 'rb').read()
    motif = args[1] if len(args) > 1 else 'ois'
    vus = set()
    for i, dtb in entrees(b):
        if seule is not None and i != seule:
            continue
        dts = subprocess.run(['dtc', '-q', '-I', 'dtb', '-O', 'dts', '-'], input=dtb,
                             capture_output=True).stdout.decode(errors='replace')
        modele = re.search(r'model = "([^"]*)"', dts)
        ids = re.findall(r'qcom,board-id = <([^>]*)>', dts)
        trouves = noeuds(dts, motif)
        if not trouves:
            continue
        print(f'===== entrée {i} : {modele.group(1) if modele else "?"} board-id {ids}')
        for chemin, bloc in trouves:
            cle = (chemin, bloc)
            if cle in vus:
                print(f'--- {chemin} (identique à une entrée précédente)')
                continue
            vus.add(cle)
            print(f'--- {chemin}\n{bloc}')


if __name__ == '__main__':
    main()
