# Reconnaissance d'empreintes côté Linux (`gfreco`)

`gfdit` (TrustZone) refuse d'enrôler sans la clé keymaster (voir `../README.md`).
Ce dossier contourne le problème : on ne garde de `gfdit` que **l'image brute**
du capteur, et la reconnaissance se fait entièrement sous Linux.

**État (2026-11-06) : chaîne complète écrite, mais NON calibrée.** Voir
« Limites » : les seules captures disponibles (un seul doigt, poses d'usine
20/50, pas encore de capture 1002/3) ne permettent pas de fixer un seuil fiable.

## Fichiers

- `gf-capture.sh` : allume le HBM local, lance la capture d'usine, éteint.
- `gfreco.py` : mise à plat, amélioration des crêtes, corrélation, minuties,
  base, `ref` / `enroll` / `verify` / `score` / `eval`.

Dépendances (sur le téléphone) : `python-numpy python-scipy python-opencv
python-scikit-image`.

## Principe

1. **Référence** : capture sans doigt, même pose, HBM allumé. Elle porte la
   grille de micro-trous du capteur, qui domine l'image brute ; l'image avec
   doigt est divisée par elle (log du rapport). Sans cette étape, aucune
   crête n'est exploitable.
2. **Crêtes** : retrait du fond, normalisation locale, bande passante, filtre
   de Gabor orienté par le champ d'orientation.
3. **Comparaison** (deux scores, moyennés) :
   - corrélation normalisée avec recherche de rotation (±30°) et de
     décalage (±40 px), pénalisée si le recouvrement est faible ;
   - minuties (terminaisons / bifurcations du squelette), vote de Hough sur
     rotation + translation, score = appariées / √(n₁·n₂).
4. **Décision** : score fusionné ≥ `GFRECO_THRESH` (défaut 0,30).

La base est dans `~/.local/share/gfreco` (0600, modèles = images de crêtes,
donc données biométriques : ne rien publier).

## Utilisation

```sh
sudo ./gf-capture.sh ~/ref.bin 50            # SANS doigt
sudo ./gf-capture.sh ~/doigt1.bin 50         # doigt posé sur le disque
./gfreco.py ref ~/ref.bin
./gfreco.py enroll index-droit ~/doigt1.bin ~/doigt2.bin ~/doigt3.bin
./gfreco.py verify ~/essai.bin               # code retour 0 = reconnu
```

`gf-capture.sh` suppose `local_hbm` (patch 0923) et `~/qseecom-cli`. Les poses
d'usine 50 et 100 sont acceptées par `1003/18` ; la référence et les captures
avec doigt doivent avoir **la même pose**.

## Limites (honnêtes)

- Sur les captures disponibles, les crêtes sont nettes mais le champ montre
  surtout des **lignes parallèles** (pas de cœur ni de delta) : peu de
  minuties (20 à 32) et une corrélation ambiguë le long des crêtes. Mesures
  faites : même doigt à deux poses, score fusionné ≈ 0,24 ; capture sans
  doigt contre doigt ≈ 0,16–0,18. L'écart existe mais il est mince : **le
  seuil par défaut (0,30) refuserait ces captures authentiques**.
- Aucune mesure d'imposteur réel (un seul doigt disponible). Le taux de faux
  accepts est donc inconnu. **Ne pas utiliser pour déverrouiller quoi que ce
  soit** avant d'avoir lancé `eval`.
- Pas de détection de faux doigt (photo, silicone).
- Pas d'intégration (fprintd, écran de verrouillage de Plasma Mobile) : à
  faire une fois le taux d'erreur connu.

## Calibrer : jeu de données à capturer

```
~/jeu/ref.bin          capture sans doigt
~/jeu/index-d/01.bin … 10.bin     10 appuis du même doigt, en variant
                                  légèrement position et rotation
~/jeu/majeur-d/01.bin … 10.bin    autre doigt (idéalement une autre
                                  personne pour les imposteurs)
./gfreco.py eval ~/jeu
```

`eval` affiche les distributions authentiques/imposteurs, l'EER et le seuil
pour FAR ≤ 1 % et 0,1 %. Fixer ensuite `GFRECO_THRESH`.

## Pistes si les scores restent trop proches

- passer à la capture normale `1002/3` (mode 7, moins de grille) ;
- moyenner plusieurs captures à l'enrôlement ;
- minuties plus fiables (extraction par orientation, pas par squelette) ;
- appariement de petits morceaux (patches) au lieu de l'image entière ;
- prétraitement spécifique à la grille du capteur (filtre en peigne).
