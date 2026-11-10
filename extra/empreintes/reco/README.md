# Reconnaissance d'empreintes côté Linux (`gfreco`)

`gfdit` (TrustZone) refuse d'enrôler sans la clé keymaster (voir `../README.md`).
Ce dossier contourne le problème : on ne garde de `gfdit` que **l'image brute**
du capteur, et la reconnaissance se fait entièrement sous Linux.

**État (2026-11-06, soir) : chaîne complète écrite, mais elle ne sépare pas les
doigts avec la capture actuelle.** Mesure sur 20 captures réelles (2 doigts ×
10 appuis, pose 50, `gf-capture.sh`) : EER ≈ **47 %** (hasard = 50 %). Voir
« Mesures et diagnostic ». Ne pas utiliser pour déverrouiller quoi que ce soit.

## Fichiers

- `gf-capture.sh` : allume le HBM local, lance la capture d'usine, éteint.
- `gf-serie.sh` : enchaîne les captures, une par Entrée, sans relancer la commande.
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

## Mesures et diagnostic (2026-11-06)

Jeu : `ref.bin` + `index/` ×10 + `majeur/` ×10 (même personne).

| Mesure | Résultat |
|---|---|
| `eval`, version d'origine | authentiques 0,265 / imposteurs 0,258, **EER 47 %** |
| Soustraction du motif commun (validation croisée) | EER ≈ 42 % |
| Champ d'orientation seul | EER 49 % |
| Corrélation de phase (POC) | EER 45–55 % |
| Recherche étroite (±8 px, ±8°) | EER 46–58 % |
| `diag` : classement par moyenne du doigt | 13/20 (hasard 10/20) |

Second jeu (40 captures, 2 doigts × 20 appuis, même méthode) : `eval` EER
**47,2 %**, `diag` 27/40 (hasard 20/40) ; énergie propre à chaque doigt 115
contre 3943 de variabilité d'un appui à l'autre. Retirer le mode commun par
projection (1 à 8 composantes, estimées sur l'autre moitié des captures)
donne un EER de 49–52 % : même verdict. Le signal propre au doigt existe à
peine (27/40 est un peu au-dessus du hasard) et reste noyé.

Ce qui est établi :

- Le signal est **reproductible dans une même capture** (corrélation 0,99
  entre moitiés de trames) et deux appuis consécutifs sont recalés à moins
  de 1 px : le doigt est bien posé au même endroit.
- Mais **47 % de l'énergie de l'image est un motif commun à tous les
  appuis et tous les doigts** (grille du capteur mal éliminée par la
  référence, moiré avec les pixels de la dalle) : une corrélation brute
  donne 0,43 entre deux appuis du même doigt comme entre deux doigts.
- Une fois ce motif retiré, la différence entre les deux doigts est
  **17 fois plus petite** que la variabilité d'un appui à l'autre
  (199 contre 3451). Les crêtes visibles ne sont pas reproductibles d'un appui à
  l'autre (pression, humidité, couplage optique) ; aucun algorithme de
  comparaison ne peut séparer ce qui n'est pas dans l'image.
- Ne **pas** estimer le motif commun sur des images du doigt à enrôler :
  la pose étant répétable, on retire alors les crêtes elles-mêmes.

Conclusion : le problème est la **capture**, pas la comparaison. La capture
d'usine `1003/21` (5 trames, une pose, ratio à une référence sans doigt) ne
suffit pas.

## Pistes (par ordre de rentabilité)

0. **Essai physique, sans code** : doigt légèrement humide, appui franc et
   constant (un doigt sec ou un appui léger donne peu de contraste de
   crêtes sur un capteur optique). Refaire une série de 10 appuis par doigt,
   puis `diag`. Aussi : pose 100 au lieu de 50.

Note : `1002/3` (capture normale) ne renvoie qu'un **statut** (40 octets) ;
l'image corrigée reste dans la TA et n'alimente que `1001/x` (algo). Elle
n'est donc pas lisible directement depuis Linux ; seules les commandes
d'usine `1003/…` rendent des pixels. La piste 1 ci-dessous suppose d'en
trouver une qui renvoie l'image traitée (`1003/38` « localAreaSample »,
20544 octets, à examiner).

1. **Capture normale `1002/3` (mode 7)**, celle du HAL Android : le pilote
   l'a débloquée, ses images ont une chaîne de correction propre à Goodix
   (éclairé / sombre, étalonnage). Mettre `gf-capture.sh` sur cette commande
   et relancer `gfreco.py diag`.
2. **Motif fixe dédié** : capture d'une peau sans crêtes (dos de la main,
   avant-bras) ou d'un adhésif uniforme posé sur l'écran, à utiliser comme
   référence à la place du « sans doigt ».
3. Plusieurs poses d'exposition (20/50/100) moyennées ; appuis plus francs et
   plus constants.
4. Seulement ensuite : retoucher la comparaison (minuties plus fiables,
   appariement par morceaux).

À chaque nouvelle capture : lancer `diag` d'abord. Si le classement reste
proche du hasard, inutile de toucher à `eval`.

## Limites

- Pas de détection de faux doigt (photo, silicone).
- Pas d'intégration (fprintd, écran de verrouillage de Plasma Mobile).
- Seuil par défaut (0,30) arbitraire, à fixer par `eval` quand le diagnostic
  sera bon.

## Capturer une série

```sh
sudo ./gf-serie.sh -r ~/jeu/ref.bin       # référence, sans doigt
sudo ./gf-serie.sh ~/jeu/index 10         # 10 appuis du même doigt
sudo ./gf-serie.sh ~/jeu/majeur 10        # autre doigt
```

À chaque invite : pose le doigt, Entrée (compte à rebours de 2 s), la capture
part, l'invite suivante revient. `q` + Entrée termine. Relancer sur le même
dossier continue la numérotation.

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

## Suppression de la grille du capteur (2026-11-10)

La capture brute est dominée par la grille régulière de points du capteur,
dans la même bande de fréquences que les crêtes. `gfreco.py` apprend
maintenant un filtre coupe-bande (notch FFT) sur la référence sans doigt —
qui ne contient que cette grille — et le retire de chaque image avant
l'extraction des crêtes (`build_notch`, `denotch`).

Validé sur images synthétiques (grille large bande déphasée à chaque appui,
le cas réaliste) : sans filtre EER ≈ 53 % (imposteurs au-dessus des vrais,
pire que le hasard) ; avec filtre EER ≈ 36 % et séparation du bon côté. Pas
encore utilisable, mais la direction est bonne ; le reste se joue à la
capture (variabilité d'un appui à l'autre). **À confirmer sur un vrai jeu**
avec `diag` puis `eval`.

## Pistes si les scores restent trop proches

- passer à la capture normale `1002/3` (mode 7, moins de grille) ;
- moyenner plusieurs captures à l'enrôlement ;
- minuties plus fiables (extraction par orientation, pas par squelette) ;
- appariement de petits morceaux (patches) au lieu de l'image entière ;
- prétraitement spécifique à la grille du capteur (filtre en peigne).
