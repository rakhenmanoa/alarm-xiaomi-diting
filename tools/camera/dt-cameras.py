#!/usr/bin/python3 -I
"""Affiche le câblage des caméras dans le device tree Android du diting :
nœuds qcom,cam-sensor / eeprom / actuator / flash / cam-res-mgr, CSIPHY,
CCI, MCLK, GPIO, régulateurs, avec les références résolues.

  dt-cameras.py device-data/partitions/dtbo_a.img [vendor_boot_a.img ...]

Lit tous les blocs FDT (magic d00dfeed) des fichiers donnés, sans dtc.
Dans un overlay (dtbo), les références vers la base (&tlmm, &L5B...)
sont résolues par __fixups__ : elles s'affichent « &étiquette ».
Les références internes sont résolues par leur chemin de nœud.
"""
import re
import struct
import sys

FDT_MAGIC = 0xd00dfeed
BEGIN, END, PROP, NOP, FIN = 1, 2, 3, 4, 9

INTERESSANTS = re.compile(r'cam-sensor|eeprom|actuator|cam-res-mgr|camera-flash|'
                          r'qcom,csiphy|qcom,cci|cam_|wl2866|camera|ois', re.I)
# propriétés dont le premier mot de chaque groupe est un phandle
PHANDLE_PROPS = re.compile(r'(-supply|^gpios$|-gpios?$|^clocks$|^pinctrl-\d+$|'
                           r'^cam_|^interconnects|^power-domains|^gpio-|^regulator$)')


def blocs(data):
    i = 0
    while True:
        i = data.find(b'\xd0\x0d\xfe\xed', i)
        if i < 0:
            return
        if i + 40 <= len(data):
            total, = struct.unpack_from('>I', data, i + 4)
            if 40 < total <= len(data) - i:
                yield i, data[i:i + total]
                i += total
                continue
        i += 4


def analyser(fdt):
    _, total, off_struct, off_strings, _, _, _, _, size_strings, size_struct = struct.unpack_from('>10I', fdt, 0)
    strings = fdt[off_strings:off_strings + size_strings]
    nom_str = lambda o: strings[o:strings.index(b'\0', o)].decode(errors='replace')
    p = off_struct
    pile, noeuds = [], []
    while True:
        tok, = struct.unpack_from('>I', fdt, p); p += 4
        if tok == BEGIN:
            fin = fdt.index(b'\0', p)
            nom = fdt[p:fin].decode(errors='replace')
            p = (fin + 4) & ~3
            if not pile:
                chemin = '/'
            elif pile[-1][0] == '/':
                chemin = '/' + nom
            else:
                chemin = pile[-1][0] + '/' + nom
            props = {}
            pile.append((chemin, props))
            noeuds.append((chemin, props))
        elif tok == END:
            pile.pop()
        elif tok == PROP:
            ln, no = struct.unpack_from('>II', fdt, p); p += 8
            pile[-1][1][nom_str(no)] = fdt[p:p + ln]
            p = (p + ln + 3) & ~3
        elif tok == NOP:
            continue
        elif tok == FIN:
            break
        else:
            raise ValueError(f'jeton FDT inconnu {tok}')
    return noeuds


def groupe(prop):
    """Nombre de cellules par référence (phandle + arguments), estimé."""
    if prop == 'clocks':
        return 2
    if prop == 'gpios' or prop.endswith('-gpios') or prop.endswith('-gpio'):
        return 3
    return 1


def chaine(v):
    if v and v[-1] == 0 and all(32 <= c < 127 or c == 0 for c in v) and b'\0\0' not in v:
        return ', '.join(repr(s.decode()) for s in v[:-1].split(b'\0'))
    return None


def main():
    vus = {}
    for fichier in sys.argv[1:]:
        data = open(fichier, 'rb').read()
        for n_bloc, (pos, fdt) in enumerate(blocs(data)):
            try:
                noeuds = analyser(fdt)
            except (ValueError, struct.error) as e:
                print(f'# {fichier} bloc @0x{pos:x} illisible : {e}')
                continue
            par_chemin = {c: p for c, p in noeuds}
            phandles = {}
            for c, p in noeuds:
                for k in ('phandle', 'linux,phandle'):
                    if k in p and len(p[k]) == 4:
                        phandles[struct.unpack('>I', p[k])[0]] = c
            # overlay : "chemin:propriété:décalage" → étiquette de la base
            fixups = {}
            for c, p in noeuds:
                if c.endswith('/__fixups__'):
                    for etiquette, val in p.items():
                        for ref in val.rstrip(b'\0').split(b'\0'):
                            fixups[ref.decode(errors='replace')] = etiquette
            modele = par_chemin.get('/', {}).get('model', b'')
            choisis = []
            for c, p in noeuds:
                compat = p.get('compatible', b'').decode(errors='replace')
                nom = c.rsplit('/', 1)[-1]
                if c.startswith('/__') or c.endswith('/__overlay__') or '/' not in c[1:] and c.startswith('/fragment'):
                    continue
                if INTERESSANTS.search(compat) or INTERESSANTS.search(nom):
                    choisis.append((c, p))
            if not choisis:
                continue
            # les dtbo ont souvent plusieurs copies (une par révision de carte)
            empreinte = hash(tuple((c, tuple(sorted(p.items()))) for c, p in choisis))
            if empreinte in vus:
                print(f'##### {fichier} bloc {n_bloc} @0x{pos:x} : nœuds caméra identiques au bloc {vus[empreinte]}')
                continue
            vus[empreinte] = f'{fichier} {n_bloc}'
            print(f'##### {fichier} bloc {n_bloc} @0x{pos:x} {chaine(modele) or ""}')
            for c, p in choisis:
                m = re.match(r'(/fragment@\d+)/__overlay__', c)
                cible = fixups.get(f'{m.group(1)}:target:0') if m else None
                print(f'{c}' + (f'    [dans &{cible}]' if cible else ''))
                for k, v in p.items():
                    if k in ('phandle', 'linux,phandle'):
                        continue
                    s = chaine(v)
                    if s is not None:
                        print(f'    {k} = {s}')
                        continue
                    mots = [struct.unpack_from('>I', v, i)[0] for i in range(0, len(v) - len(v) % 4, 4)]
                    rendu = []
                    for i, m in enumerate(mots):
                        ref = fixups.get(f'{c}:{k}:{4 * i}')
                        if ref:
                            rendu.append('&' + ref)
                        elif PHANDLE_PROPS.search(k) and i % groupe(k) == 0 and m in phandles:
                            rendu.append('&{' + phandles[m] + '}')
                        else:
                            rendu.append(f'0x{m:x}' if m > 0xffff else str(m))
                    print(f'    {k} = <{" ".join(rendu)}>' if mots else f'    {k}')
            print()


if __name__ == '__main__':
    main()
