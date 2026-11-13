# GPU du Xiaomi 12T Pro (diting) — tension, fréquences, bande passante mémoire

Ce dossier documente ce qui a été fait (et découvert) sur le GPU Adreno 730v3 du SM8475 : le réglage
de tension, la table de fréquences, et le patch qui fait enfin voter la bande passante mémoire au GPU.

> **Ce sont des copies de référence, figées le 2026-11-13.** Les fichiers réellement construits
> restent ceux de `pkgbuilds/linux-diting/` : mettez d'abord ces originaux à jour, puis recopiez ici.

## Contenu

| Fichier | Contenu | Original |
|---|---|---|
| `patches/0926-arm64-dts-qcom-sm8475-GPU-DDR-bandwidth-votes.patch` | Chemin `gfx-mem` et `opp-peak-kBps` pour les 13 niveaux du GPU | `pkgbuilds/linux-diting/0926-…` |

Le patch s'applique sur `arch/arm64/boot/dts/qcom/sm8475.dtsi`, **fichier qui vient de la série de
patches de royka1/diting-ubuntu** (il n'existe pas dans ce dépôt). Il passe donc après cette série dans
le `PKGBUILD` de `linux-diting`.

## 1. « Régler la tension du GPU » : ce n'est pas possible sous Linux

Résultat de l'enquête, mesuré sur le téléphone (`/proc/device-tree/soc@0/gpu@3d00000/opp-table/`) :

- Chaque niveau OPP du GPU n'a que `opp-hz` et `opp-level`. **Il n'y a pas de `opp-microvolt`.**
- Le GMU (le microcontrôleur du GPU) reçoit la table des niveaux au démarrage par HFI. Il ne vote pas
  des microvolts mais des **niveaux ARC** (`opp-level`), via RPMh et cmd-db.
- La tension réelle de chaque niveau est fixée par le **firmware AOP** et le PMIC (signés, non modifiables).
  Modifier un `opp-microvolt` dans le DTB resterait donc sans effet.
- Patcher le firmware du GMU (`a730_gmu.bin`, fermé) ne changerait pas non plus la tension : il enverrait
  seulement d'autres niveaux à l'AOP.

Ce qu'on peut régler :

1. **Le plafond de fréquence** (`max_freq` du devfreq) : la tension suit le niveau voté, donc plafonner
   la fréquence baisse aussi la tension. C'est le moyen sûr.
2. **Le niveau associé à une fréquence** (`opp-level` dans le DTS). Risqué : si la fréquence n'est pas
   stable au niveau choisi, le GPU plante ou gèle. À tester niveau par niveau.

### Les 13 niveaux du SM8475

Table de `sm8475.dtsi` (les valeurs numériques sont celles lues dans le DT vivant, 4 octets gros-boutiste) :

| Fréquence | `opp-level` | Valeur |
|---|---|---|
| 220 MHz | `MIN_SVS` | 48 |
| 285 MHz | `LOW_SVS_D1` | 56 |
| 324 MHz | `LOW_SVS` | 64 |
| 364 MHz | `LOW_SVS_L1` | 80 |
| 439 MHz | `LOW_SVS_L2` | 96 |
| 515 MHz | `SVS` | 128 |
| 580 MHz | `SVS_L0` | 144 |
| 645 MHz | `SVS_L1` | 192 |
| 710 MHz | `SVS_L2` | 224 |
| 765 MHz | `NOM` | 256 |
| 815 MHz | `NOM_L1` | 320 |
| 862 MHz | `TURBO` | 384 |
| 900 MHz | `TURBO_L1` | 416 |

Chaque fréquence a son propre niveau (aucun n'est partagé). Source des fréquences et des niveaux : les
`qcom,gpu-pwrlevels` du kgsl d'Android, repris dans `sm8475.dtsi`.

Piège rencontré : un premier décodage « de mémoire » des valeurs numériques avait donné de mauvais noms.
Se fier aux noms symboliques de `sm8475.dtsi`, pas à une table de constantes apprise par cœur.

## 2. Ce qui manquait : le GPU ne votait aucune bande passante mémoire

Constat (`/sys/kernel/debug/interconnect/interconnect_summary`) : aucune ligne `3d00000.gpu`, et
`qnm_gpu@19100000` sans client. La cause est dans le DTS :

- le nœud `gpu@3d00000` de `sm8450.dtsi` n'a **pas** de propriété `interconnects` ;
- les niveaux OPP n'ont **pas** de `opp-peak-kBps`.

Conséquence : quand le GPU travaillait seul, rien ne faisait monter la DDR pour lui.

### Le patch `0926`

Ajoute, dans `&gpu` de `sm8475.dtsi`, comme sur `sm8550.dtsi` :

```dts
interconnects = <&gem_noc MASTER_GFX3D QCOM_ICC_TAG_ALWAYS
                 &mc_virt SLAVE_EBI1 QCOM_ICC_TAG_ALWAYS>;
interconnect-names = "gfx-mem";
```

et un `opp-peak-kBps` par niveau :

| Niveaux | `opp-peak-kBps` |
|---|---|
| 900 MHz | 16500000 |
| 862, 815 MHz | 12449218 |
| 765, 710 MHz | 10687500 |
| 645, 580, 515 MHz | 6074218 |
| 439, 364, 324, 285, 220 MHz | 2136718 |

**Ces valeurs sont une estimation de départ** : les paliers de `sm8550.dtsi` (`16500000`, `12449218`,
`10687500`, `6074218`, `2136718`) répartis par rang sur les 13 niveaux. Ce ne sont pas des valeurs du
SM8475. À ajuster par mesure si la DDR monte trop haut.

Macros vérifiées dans l'arbre : `MASTER_GFX3D` = 3 et `SLAVE_EBI1` = 1 (`qcom,sm8450.h`), labels
`gem_noc` et `mc_virt` présents dans `sm8450.dtsi`.

## 3. Tester sans reconstruire le noyau (avec `fdtput`)

Utile pour essayer le patch sur le DTB déjà installé. À faire **sur le DTB de `/boot/dtbs/`**, pas sur
celui d'un arbre noyau incomplet (voir § 5).

Vérifier d'abord que les phandles sont ceux du DTB de l'arbre (`46` et `30`) :

```sh
fdtget -t x /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /soc@0/interconnect@19100000 phandle
fdtget -t x /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /interconnect-1 phandle
```

Sauvegarde, puis le chemin mémoire (0x46 = 70, 0x30 = 48) :

```sh
sudo cp /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb.avant-gpu
sudo fdtput -t u /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /soc@0/gpu@3d00000 interconnects 70 3 7 48 1 7
sudo fdtput -t s /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /soc@0/gpu@3d00000 interconnect-names gfx-mem
```

Puis une ligne par niveau, par exemple pour le plus haut :

```sh
sudo fdtput -t u /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /soc@0/gpu@3d00000/opp-table/opp-900000000 opp-peak-kBps 16500000
```

Enfin :

```sh
sudo mkinitcpio -P
sudo diting-install-bootimg
sudo reboot
```

Retour arrière : remettre `sm8475-xiaomi-diting.dtb.avant-gpu`, puis `mkinitcpio -P` et
`diting-install-bootimg`.

Note : `fdtput` ajoute les propriétés en fin de nœud. Un `diff` ligne à ligne contre un DTB compilé
montre donc des lignes déplacées. Comparer le contenu trié :

```sh
diff <(dtc -I dtb -O dts -q A.dtb 2>/dev/null | grep "opp-peak\|interconnect" | sort) \
     <(dtc -I dtb -O dts -q B.dtb 2>/dev/null | grep "opp-peak\|interconnect" | sort) && echo IDENTIQUE
```

## 4. Vérifier que ça marche

Après redémarrage, en root, pendant que le GPU travaille :

```sh
sudo grep -i -B1 -A2 "3d00000\|qnm_gpu" /sys/kernel/debug/interconnect/interconnect_summary
```

Résultat obtenu le 2026-11-13 : `3d00000.gpu` apparaît comme client de `qnm_gpu`, avec un pic de
**6074218 kBps** (niveau du milieu de la table), et ce pic remonte jusqu'à la DDR (`ebi` et `llcc_mc`,
dont le pic passait de 700000 à 6074218). Le GPU vote donc bien sa bande passante.

## 5. Pièges

- **Ne pas copier le DTB compilé dans `linux-7.2.10/`** : cet arbre n'a pas tous les patches de ce
  dépôt. Comparé au DTB installé, il fait régresser les micros (`ports-lane-control`, `ports-offset1`,
  patch 0903), le flash arrière (`status = "disabled"`, patch 0912) et les alimentations du capteur
  d'empreintes (`regulator-always-on` absent, patch 0922). La voie propre est le paquet `linux-diting`.
- **Tabulations, pas espaces.** `sm8475.dtsi` est indenté en tabulations ; un premier patch écrit avec des
  espaces échouait (`Hunk FAILED`). Vérifier avec `sed -n 'A,Bp' fichier | cat -A`.
- **Numéros de ligne** : tester le patch avec `patch -p1 --dry-run` avant de l'appliquer.
- L'avertissement de démarrage `disp_cc_mdss_mdp_clk_src: rcg didn't update its configuration`
  (`dispcc_sm8450`) n'a aucun rapport avec le GPU ni avec ces patches.

## 6. Résultat sur la fluidité

Mesures du 2026-11-13 :

- Plasma Mobile : un peu plus fluide avec le vote de bande passante.
- Firefox : **GPU à 5 % de charge** pendant un défilement, à 645 MHz ; les trois groupes de cœurs
  montent bien en fréquence. Le GPU n'est donc **pas** le goulot de Firefox ; ni la bande passante ni la
  tension n'y changeront grand-chose.

Pistes restantes pour la fluidité (hors GPU) : le nombre de pixels rendus (écran 2712 × 1220, session
Plasma en basse résolution agrandie par le contrôleur d'affichage), et le chemin d'affichage de KWin
(cadence des images, copies inutiles).

Autre limite connue : Wild Life plante à cause de la préemption (voir §7).

Leviers GPU encore disponibles, à utiliser seulement si un usage est limité par le GPU (Waydroid,
jeux) : fixer `min_freq` du devfreq (montée plus rapide, au prix de la batterie), régler le gouverneur,
mettre à jour Mesa (freedreno/turnip), vérifier qu'aucun plafond thermique ne bride le GPU.

```sh
cat /sys/class/devfreq/*gpu*/{governor,min_freq,max_freq,cur_freq,available_frequencies}
```

## 7. Le plantage de 3DMark Wild Life : la préemption de l'A730

Symptôme (Waydroid, 3DMark → Wild Life, Vulkan) : l'appli meurt en quelques secondes, parfois avec des
barres à l'écran, et le noyau écrit

```
adreno 3d00000.gpu: [drm:a6xx_irq [msm]] *ERROR* gpu fault ring 2 fence … status 00E01007 …
msm_dpu …: 07030001: hangcheck recover!
msm_dpu …: offending task: Thread-12 (com.futuremark.dmandroid.application)
```

Reproductible à chaque lancement, **avant comme après** les patches 0926 à 0929, quelle que soit la
fréquence (plafonner à 710 MHz n'y change rien). Sling Shot Vulkan, plus léger, passe sans problème. Le
message « missing vulkan features » (test OMM) est sans rapport : Wild Life ne demande que Vulkan 1.1.

### Capture et décodage

Le pilote `msm` dépose l'état du GPU dans un *devcoredump*, qui disparaît après environ 5 minutes :

```sh
cat /sys/class/devcoredump/disabled          # 0 = capture active
# lancer Wild Life jusqu'au plantage, puis tout de suite :
sudo timeout 300 sh -c 'cat /sys/class/devcoredump/devcd2/data > /tmp/hang.txt' &
ls -l /tmp/hang.txt                          # attendre que la taille se stabilise (~14 Mo)
```

Le fichier est du YAML avec des registres bruts. `scripts/compiler-crashdec.sh` compile `crashdec`
(Mesa, `-Dtools=freedreno`) sur un PC ; `crashdec -f hang.txt` décode les registres. **Attention** : la
sortie dépasse 1 Go (décodage des tampons), la limiter avec `head` ou la couper après la section
`registers:`.

### Ce que montre la capture

- `CP_RB_BASE` = anneau 0, `CP_RB_RPTR` = `CP_RB_WPTR` = `0xc9c` : le processeur de commandes a **tout
  lu** sur l'anneau 0, `CP_SCRATCH[2]` = 3173 (la dernière fence de cet anneau).
- `RBBM_STATUS` ne contient que `CP_AHB_BUSY_CX_MASTER` ; `RBBM_INT_0_STATUS`, `CP_HW_FAULT`,
  `CP_INTERRUPT_STATUS` à 0 : aucun bloc occupé, aucune faute matérielle.
- L'anneau 2 a du travail en attente (fences 79037 → 79042, 439 mots non lus) que le CP n'a jamais
  commencé.

Le GPU est donc resté sur l'anneau 0, vide, sans passer à l'anneau 2 : la **préemption** (changement
de contexte entre anneaux) ne s'est pas faite. Dans le noyau 7.2, l'A730 porte `ADRENO_QUIRK_PREEMPTION`
(`a6xx_catalog.c`) et utilise donc 4 anneaux.

### Contournement (validé le 2026-11-13)

Le paramètre `enable_preemption` du module `msm` (1 = actif, 0 = coupé, −1 = auto, valeur par défaut) :

```sh
echo 'options msm enable_preemption=0' | sudo tee /etc/modprobe.d/msm-sans-preemption.conf
sudo mkinitcpio -P && sudo reboot
cat /sys/module/msm/parameters/enable_preemption     # 0
```

Avec `0`, Wild Life va jusqu'au bout. Le paquet `diting-device` (≥ 0.1.25-2) installe ce réglage dans
`/usr/lib/modprobe.d/diting-gpu-preemption.conf`. Contrepartie : sans préemption, une charge GPU très
longue peut faire attendre brièvement les autres clients (l'interface). Pour revenir au comportement du
noyau : supprimer ce fichier (et le `/etc/modprobe.d` de test), `mkinitcpio -P`, redémarrer.

### Score de référence

Une fois la préemption coupée, 3DMark Wild Life (Vulkan, via Waydroid, Mesa 26.0.8 de l'image, noyau
7.2.10-36 avec les patches 0926 à 0929) donne **8837** points (2026-11-13). Pour situer : le Poco F5 Pro
(Snapdragon 8+ Gen 1, même Adreno 730) a mesuré 8097 (Wild Life) et 8291 (Unlimited) selon Gizmochina,
et un Galaxy S22+ (Snapdragon 8 Gen 1) 9067 selon UL. Wild Life ne dure qu'une minute : c'est un score de
pointe, pas de tenue thermique. À refaire avec le même test après tout nouveau réglage GPU.

## Voir aussi

- [cpu/README.md](../cpu/README.md) : les moniteurs de bande passante du CPU (`bwmon`), patch `0927`.
- [screen/README.md](../screen/README.md) : l'affichage, où se joue l'essentiel de la fluidité.
