#!/usr/bin/env python3
"""Décode l'EEPROM de calibration du module photo principal (Semco, S5KHP1).

L'EEPROM est une Puya P24C128F (16 Ko) à l'adresse 0x51 du bus CCI du HP1,
déjà gérée par le pilote at24 :

    sudo cat /sys/bus/i2c/devices/3-0051/eeprom > ~/hp1-eeprom.bin

Ces données sont propres à chaque téléphone : ne pas mettre le fichier brut
dans le dépôt (il contient aussi le numéro de série du module).

Organisation relevée (blocs « 00 id 01 … somme ») : la somme de contrôle est
la somme des octets depuis l'octet 01 (validité) jusqu'à l'octet qui précède
la somme, modulo 255.

    0x0000  infos module (fabricant, date)     somme en 0x0024
    0x0025  bloc 01 (inconnu, 12 octets)
    0x0045  bloc 02 (mise au point ? 0x02a0, 0x025e, 0x03ff)
    0x0060  bloc 03 ombrage (LSC) 17 x 13, R Gr Gb B, 16 bits
    0x0770  bloc 04 balance des blancs : module puis référence, R Gr Gb B
    0x07a0  bloc 05 (1,9 Ko, inconnu : diaphonie / remosaïquage ?)
    0x0f10  bloc 06 (5 octets, inconnu)
    0x0f30  bloc 07 PDAF : cartes de gain gauche/droite 17 x 13
    0x1450  bloc 09 PDAF « PD01 » (coefficients de défocalisation ?)
    0x1aa0  bloc 0b PDAF « PDXT » (diaphonie des pixels de phase ?)
    0x3e50  identifiants Xiaomi, numéro de série du module

Exemples :
    hp1-eeprom.py ~/hp1-eeprom.bin                 # blocs et sommes
    hp1-eeprom.py ~/hp1-eeprom.bin --lsc           # table d'ombrage
    hp1-eeprom.py ~/hp1-eeprom.bin --lsc-gains     # gains de correction (centre = 1)
    hp1-eeprom.py ~/hp1-eeprom.bin --awb [--noir 64]
    hp1-eeprom.py ~/hp1-eeprom.bin --pdaf
"""
import argparse
import sys

LSC_W, LSC_H = 17, 13
CANAUX = ("R", "Gr", "Gb", "B")

# (nom, début de l'en-tête « 00 id », position de la somme)
BLOCS = [
    ("infos module", 0x0000, 0x0024),
    ("bloc 01", 0x0025, 0x0043),
    ("bloc 02", 0x0045, 0x0051),
    ("bloc 03 ombrage (LSC)", 0x0060, 0x074F),
    ("bloc 04 balance des blancs", 0x0770, 0x0786),
    ("bloc 05", 0x07A0, 0x0EF3),
    ("bloc 06", 0x0F10, 0x0F1F),
    ("bloc 07 PDAF cartes de gain", 0x0F30, 0x1440),
]


def be16(b, off, n):
    return [(b[off + 2 * i] << 8) | b[off + 2 * i + 1] for i in range(n)]


def somme_ok(b, debut, pos):
    depart = 0 if debut == 0 else debut + 2
    return sum(b[depart:pos]) % 255 == b[pos]


def lsc(b):
    """Retourne 4 grilles (R, Gr, Gb, B) de LSC_H lignes x LSC_W colonnes."""
    v = be16(b, 0x67, LSC_W * LSC_H * 4)
    return [[[v[(y * LSC_W + x) * 4 + c] for x in range(LSC_W)] for y in range(LSC_H)]
            for c in range(4)]


def afficher_grille(nom, g, fmt="{:5d}"):
    print(nom)
    for ligne in g:
        print(" ".join(fmt.format(x) for x in ligne))
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fichier")
    ap.add_argument("--lsc", action="store_true", help="table d'ombrage brute")
    ap.add_argument("--lsc-gains", action="store_true",
                    help="gains de correction : centre du canal / valeur")
    ap.add_argument("--awb", action="store_true", help="balance des blancs d'usine")
    ap.add_argument("--noir", type=float, default=0,
                    help="niveau de noir à retirer des moyennes AWB (10 bits, ex. 64)")
    ap.add_argument("--pdaf", action="store_true", help="cartes de gain PDAF")
    a = ap.parse_args()

    b = open(a.fichier, "rb").read()
    if len(b) != 16384:
        sys.exit(f"taille inattendue : {len(b)} octets (16384 attendus)")

    print("fabricant 0x%02x, date 20%02d-%02d-%02d" % (b[1], b[2], b[3], b[4]))
    for nom, debut, pos in BLOCS:
        print("  %-28s 0x%04x  somme %s" % (nom, debut,
              "ok" if somme_ok(b, debut, pos) else "FAUSSE"))
    print()

    if a.lsc or a.lsc_gains:
        grilles = lsc(b)
        for nom, g in zip(CANAUX, grilles):
            if a.lsc:
                afficher_grille(nom, g)
            else:
                centre = g[LSC_H // 2][LSC_W // 2]
                afficher_grille(nom + " (gain)", [[centre / x for x in l] for l in g],
                                "{:5.2f}")

    if a.awb:
        v = be16(b, 0x0776, 8)
        for nom, m in (("module", v[:4]), ("référence", v[4:])):
            r, gr, gb, bl = (x - a.noir for x in m)
            g = (gr + gb) / 2
            print("%-10s R %d Gr %d Gb %d B %d   R/G %.3f B/G %.3f   gains R %.3f B %.3f"
                  % (nom, *m, r / g, bl / g, g / r, g / bl))
        print()

    if a.pdaf:
        lw, lh = be16(b, 0x0F36, 2)
        v = be16(b, 0x0F3A, 2 * lw * lh)
        for i, nom in enumerate(("gauche", "droite")):
            g = [[v[i * lw * lh + y * lw + x] for x in range(lw)] for y in range(lh)]
            afficher_grille(f"PDAF gain {nom} ({lw} x {lh})", g)


if __name__ == "__main__":
    main()
