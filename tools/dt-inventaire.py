#!/usr/bin/python3 -I
"""Inventaire du matériel décrit dans un device tree Android (dtbo / vendor_boot).

  dt-inventaire.py -m MOTIF FICHIER [FICHIER ...] > inventaire.txt

-m MOTIF : ne garde que les blocs dont le modèle correspond (sans casse),
           ex. -m diting pour le dtbo, -m 'Cape SoC$' pour vendor_boot.
-n MOTIF : détail complet (toutes les propriétés, sous-nœuds compris) des
           nœuds dont le chemin correspond, ex. -n ir-spi.

Une ligne par nœud qui porte un « compatible » :
  état  chemin  [dans &cible]  compatible  reg=…
Les nœuds désactivés (status = disabled) sont marqués « off ».
Lit les blocs FDT sans dtc, en réutilisant camera/dt-cameras.py.
"""
import importlib.util
import os
import re
import struct
import sys

_ici = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location('dtcam', os.path.join(_ici, 'camera', 'dt-cameras.py'))
dtcam = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(dtcam)


def mots(v):
    return [struct.unpack_from('>I', v, i)[0] for i in range(0, len(v) - len(v) % 4, 4)]


def main():
    args = sys.argv[1:]
    motif = noeud = None
    while args[:1] in (['-m'], ['-n']):
        if args[0] == '-m':
            motif = re.compile(args[1], re.I)
        else:
            noeud = re.compile(args[1], re.I)
        args = args[2:]
    if not args:
        sys.exit(__doc__)
    for fichier in args:
        data = open(fichier, 'rb').read()
        for n_bloc, (pos, fdt) in enumerate(dtcam.blocs(data)):
            try:
                noeuds = dtcam.analyser(fdt)
            except (ValueError, struct.error):
                continue
            # modèle brut, sans guillemets : -m 'Cape SoC$' doit fonctionner
            modele = dict(noeuds).get('/', {}).get('model', b'').rstrip(b'\0').decode(errors='replace')
            if motif and not motif.search(modele):
                continue
            fixups = {}
            for c, p in noeuds:
                if c.endswith('/__fixups__'):
                    for etiquette, val in p.items():
                        for ref in val.rstrip(b'\0').split(b'\0'):
                            fixups[ref.decode(errors='replace')] = etiquette
            print(f'##### {fichier} bloc {n_bloc} @0x{pos:x} {modele}')
            if noeud:
                choisis = [c for c, _ in noeuds if noeud.search(c)]
                for c, p in noeuds:
                    if any(c == x or c.startswith(x + '/') for x in choisis):
                        print(c)
                        for k, v in p.items():
                            r = dtcam.chaine(v)
                            if r is None:
                                r = '<' + ' '.join(f'0x{m:x}' if m > 0xffff else str(m) for m in mots(v)) + '>' if v else ''
                            print(f'    {k}' + (f' = {r}' if r else ''))
                print()
                continue
            for c, p in noeuds:
                if c.startswith('/__') or '/__fixups__' in c or '/__local_fixups__' in c:
                    continue
                compat = p.get('compatible')
                if not compat:
                    continue
                compat = dtcam.chaine(compat) or compat.hex()
                status = dtcam.chaine(p.get('status', b'')) or ''
                etat = 'off' if 'disabled' in status else 'on '
                m = re.match(r'(/fragment@\d+)/__overlay__', c)
                cible = fixups.get(f'{m.group(1)}:target:0') if m else None
                chemin = re.sub(r'^/fragment@\d+/__overlay__', '', c) or '/'
                reg = p.get('reg')
                reg = ' reg=' + ','.join(f'0x{x:x}' for x in mots(reg)[:4]) if reg else ''
                print(f'{etat} {chemin}' + (f'  [&{cible}]' if cible else '') + f'  {compat}{reg}')
            print()


if __name__ == '__main__':
    main()
