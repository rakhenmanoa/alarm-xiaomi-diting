#!/usr/bin/python3 -I
"""Affiche les nœuds d'un device tree désignés par leur phandle numérique.

  dt-phandle.py [-m MOTIF] device-data/partitions/vendor_boot_a.img 65 69 73

Utile quand un overlay (dtbo) référence la base par phandle brut, sans
__fixups__ : ex. flash-source = <65> dans le bloc diting du dtbo_a.img.
Lit tous les blocs FDT des fichiers donnés ; -m filtre sur le modèle
(sans casse, ex. -m cape). Pour chaque phandle : chemin, propriétés,
et les sous-nœuds du parent (le contrôleur flash entier).
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util

_spec = importlib.util.spec_from_file_location(
    'dtcam', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dt-cameras.py'))
dtcam = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(dtcam)


def rendu(v):
    s = dtcam.chaine(v)
    if s is not None:
        return s
    if not v:
        return ''
    mots = [struct.unpack_from('>I', v, i)[0] for i in range(0, len(v) - len(v) % 4, 4)]
    return '<' + ' '.join(f'0x{m:x}' if m > 0xffff else str(m) for m in mots) + '>'


def afficher(chemin, props, retrait='    '):
    print(f'{retrait}{chemin}')
    for k, v in props.items():
        r = rendu(v)
        print(f'{retrait}    {k}' + (f' = {r}' if r else ''))


def main():
    args = sys.argv[1:]
    motif = None
    if args[:1] == ['-m']:
        motif = re.compile(args[1], re.I)
        args = args[2:]
    fichiers = [a for a in args if not a.isdigit()]
    cherches = [int(a) for a in args if a.isdigit()]
    for fichier in fichiers:
        data = open(fichier, 'rb').read()
        for n_bloc, (pos, fdt) in enumerate(dtcam.blocs(data)):
            try:
                noeuds = dtcam.analyser(fdt)
            except (ValueError, struct.error):
                continue
            modele = dtcam.chaine(dict(noeuds).get('/', {}).get('model', b'')) or ''
            if motif and not motif.search(modele):
                continue
            phandles = {}
            for c, p in noeuds:
                for k in ('phandle', 'linux,phandle'):
                    if k in p and len(p[k]) == 4:
                        phandles[struct.unpack('>I', p[k])[0]] = c
            trouves = [(ph, phandles[ph]) for ph in cherches if ph in phandles]
            if not trouves:
                continue
            print(f'##### {fichier} bloc {n_bloc} @0x{pos:x} {modele}')
            parents = []
            for ph, c in trouves:
                print(f'phandle {ph} → {c}')
                parent = c.rsplit('/', 1)[0]
                if parent not in parents:
                    parents.append(parent)
            for parent in parents:
                print(f'--- contrôleur {parent}')
                for c, p in noeuds:
                    if c == parent or c.startswith(parent + '/'):
                        afficher(c, p)
            print()


if __name__ == '__main__':
    main()
