#!/usr/bin/env python3
"""Calcule les seuils opp-peak-kBps des bwmon à partir des tables du vendeur.

Les tables viennent du DT d'Android (nœuds ddr-freq-table et llcc-freq-table,
propriété qcom,freq-tbl, en kHz). Convention du noyau (voir sm8550.dtsi) :
    kBps = fréquence(kHz) x largeur de bus / 1,048576, arrondi au millier inférieur
Largeurs de bus (qcom,bus-width du nœud dcvs-hw) : ddr = 4, llcc = 16.

Usage : calculer-seuils-bwmon.py              (valeurs du diting)
        calculer-seuils-bwmon.py ddr 4 0x858b8 0xbb800 ...
"""
import sys

# Valeurs lues dans le vendor_boot d'HyperOS (OS2.0.204.0.VLFMIXM)
DDR = [0x858B8, 0xBB800, 0x17BA38, 0x1A0FE0, 0x1FEBE0, 0x29BF80, 0x30C460]
LLCC = [0x493E0, 0x71C50, 0x927C0, 0xC4C70, 0xE3C88, 0x104410]


def seuils(freqs_khz, largeur):
    return [int(f * largeur / 1.048576) // 1000 * 1000 for f in freqs_khz]


def main(argv):
    if len(argv) > 3:
        nom, largeur = argv[1], int(argv[2])
        freqs = [int(x, 0) for x in argv[3:]]
        print(nom, "kHz :", freqs)
        print(nom, "kBps :", seuils(freqs, largeur))
        return
    print("DDR  kHz :", DDR)
    print("LLCC kHz :", LLCC)
    print("llcc_bwmon_opp_table (LLCC -> DDR, ddr x 4)  :", seuils(DDR, 4))
    print("cpu_bwmon_opp_table  (CPU -> LLCC, llcc x 16):", seuils(LLCC, 16))


if __name__ == "__main__":
    main(sys.argv)
