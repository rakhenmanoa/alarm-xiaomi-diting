#!/usr/bin/env python3
# Corrige le profil UCM HiFi de diting-ubuntu (testé le 2026-10-26) :
#  - chemin du micro (MultiMedia2 Mixer TX_CODEC_DMA_TX_3) activé avec le
#    profil, sinon PipeWire ne peut pas ouvrir la capture et rejette HiFi ;
#  - stéréo : la protection CS35L41 mélange DSP RX1 et RX2, et les deux
#    amplis reçoivent les canaux dans l'ordre inverse (T : RX1 = gauche,
#    B : RX1 = droite). ASPRX1 sur les deux entrées des deux amplis donne
#    gauche en haut, droite en bas ;
#  - micro du bas (ADC1) seul pour les enregistrements, sur les deux canaux :
#    avec ADC1 et ADC4 actifs ensemble, les ports SoundWire entrent encore en
#    collision à certains démarrages malgré le patch 0903 (souffle sur les
#    deux canaux), et l'ADC4 seul bloque la capture (testé le 2026-10-31).
#    Les appels gardent les deux micros (MIC_SETUP de diting-voice-call) ;
#    l'ADC4 est coupé ici pour repartir propre après un appel ;
#  - gain des décimateurs +24 dB (108 au lieu de 84) : voix vers -42 dBFS,
#    pics vers -18 dBFS sans écrêtage (ADC 20 sature à -12 dBFS).
import sys
p = sys.argv[1]
s = open(p).read()
a = "\t\tcset \"name='TERTIARY_TDM_RX_0 Audio Mixer MultiMedia1' 1\"\n"
b = "\t\tcset \"name='TERTIARY_TDM_RX_0 Audio Mixer MultiMedia1' 0\"\n"
add_on = ["\t\tcset \"name='MultiMedia2 Mixer TX_CODEC_DMA_TX_3' 1\"\n"] + [
    f"\t\tcset \"name='{amp} DSP RX{n} Source' ASPRX1\"\n" for amp in "TB" for n in (1, 2)]
add_off = ["\t\tcset \"name='MultiMedia2 Mixer TX_CODEC_DMA_TX_3' 0\"\n"]
verb = s.split('SectionDevice')[0]
assert a in verb and b in verb, "profil HiFi inattendu"
if add_on[0] not in verb:
    s = s.replace(a, a + ''.join(add_on), 1).replace(b, b + ''.join(add_off), 1)
adc4_on = ["ADC4_MIXER Switch' 1", "HDR34 MUX' NO_HDR34", "ADC4 MUX' INP5",
           "ADC4 Switch' 1", "TX3 MODE' ADC_NORMAL", "ADC4 Volume' 8"]
adc4_off = "\t\tcset \"name='ADC4 Switch' 0\"\n\t\tcset \"name='ADC4_MIXER Switch' 0\"\n"
first = "\t\tcset \"name='%s\"\n" % adc4_on[0]
if first in s:
    s = s.replace(first, adc4_off, 1)
    for l in adc4_on[1:]:
        s = s.replace("\t\tcset \"name='%s\"\n" % l, "", 1)
for src in ("SWR_MIC1", "SWR_MIC5"):
    s = s.replace(f"name='TX SMIC MUX1' {src}", "name='TX SMIC MUX1' SWR_MIC0")
for d in (0, 1):
    s = s.replace(f"name='TX_DEC{d} Volume' 84", f"name='TX_DEC{d} Volume' 108")
open(p, 'w').write(s)
