#!/usr/bin/env python3
# Corrige le profil UCM HiFi de diting-ubuntu (testé le 2026-10-26) :
#  - chemin du micro (MultiMedia2 Mixer TX_CODEC_DMA_TX_3) activé avec le
#    profil, sinon PipeWire ne peut pas ouvrir la capture et rejette HiFi ;
#  - stéréo : la protection CS35L41 mélange DSP RX1 et RX2, et les deux
#    amplis reçoivent les canaux dans l'ordre inverse (T : RX1 = gauche,
#    B : RX1 = droite). ASPRX1 sur les deux entrées des deux amplis donne
#    gauche en haut, droite en bas ;
#  - micro du haut (ADC4) sur SWR_MIC5 : avec le patch noyau 0903, le port 2
#    du codec passe sur le port maître 2 (sinon bruit blanc).
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
s = s.replace("name='TX SMIC MUX1' SWR_MIC1", "name='TX SMIC MUX1' SWR_MIC5")
open(p, 'w').write(s)
