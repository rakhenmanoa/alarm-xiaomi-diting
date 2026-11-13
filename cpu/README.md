# CPU et mémoire du Xiaomi 12T Pro (diting) — bwmon, capacité des cœurs, énergie

Ce dossier documente deux choses :

1. comment la DDR est montée en fréquence selon le **trafic mesuré du CPU**, grâce aux deux moniteurs de
   bande passante matériels du SM8475 (§ 1 à 7, patch `0927`). Tout est parti de la question « peut-on
   accélérer la mémoire ? » ;
2. comment le noyau connaît enfin la **puissance relative de chaque cœur** et le modèle d'énergie, ce qui
   permet à l'ordonnanceur de bien placer les tâches (§ 8, patch `0928`).

> **Ce sont des copies de référence, figées le 2026-11-13.** Les fichiers réellement construits restent
> ceux de `pkgbuilds/linux-diting/` et `extra/bwmon-overlay/` : mettez d'abord ces originaux à jour, puis
> recopiez ici.

## Contenu

| Fichier | Contenu | Original |
|---|---|---|
| `patches/0927-arm64-dts-qcom-sm8475-Add-the-CPU-and-LLCC-bwmon.patch` | Les deux nœuds `bwmon` dans `sm8475.dtsi` | `pkgbuilds/linux-diting/0927-…` |
| `patches/0928-arm64-dts-qcom-sm8475-CPU-capacity-and-energy-model.patch` | `capacity-dmips-mhz` et `dynamic-power-coefficient` des 8 cœurs | `pkgbuilds/linux-diting/0928-…` |
| `overlay/bwmon.dtso` | Même contenu sous forme de surcouche, pour tester sans reconstruire le noyau | `extra/bwmon-overlay/bwmon.dtso` |
| `scripts/calculer-seuils-bwmon.py` | Calcule les seuils `opp-peak-kBps` depuis les tables du vendeur | — |

Les patches `0927` et `0928` s'appliquent **après** `0926` (voir [gpu/README.md](../gpu/README.md)) et
après la série de royka1/diting-ubuntu, qui fournit `sm8475.dtsi`.

## 1. Surcadencer la DDR ? Non

La fréquence de la DDR n'est décidée ni par Linux ni par le DTS :

- le firmware **AOP/RPMh** choisit parmi une table de niveaux fixes, selon les votes de bande passante ;
- les fréquences possibles (jusqu'au LPDDR5 maximal du SM8475) sont entraînées et figées dans la chaîne de
  démarrage (XBL), signée ;
- même avec le bootloader déverrouillé, on ne dépasse pas le niveau le plus haut du firmware.

Ce qu'on peut faire : s'assurer que la DDR **atteint** son niveau le plus haut quand il le faut. Le GPU y
contribue depuis le patch `0926`. Le CPU a son propre mécanisme : le **moniteur de bande passante**
(`bwmon`), qui mesure le trafic et vote en conséquence.

## 2. Ce qui manquait

- Le pilote mainline `drivers/soc/qcom/icc-bwmon.c` (option `CONFIG_QCOM_ICC_BWMON`, compilée en
  module `icc_bwmon` dans ce noyau) gère notamment `qcom,sm8550-cpu-bwmon`, avec les données de
  `sdm845_cpu_bwmon_data`, et `qcom,sm8550-llcc-bwmon`.
- **Aucun nœud `bwmon` n'était décrit** dans `sm8450.dtsi` ni `sm8475.dtsi` : rien ne faisait monter la DDR
  selon le trafic des cœurs.

## 3. D'où viennent les valeurs : le device tree d'Android

Les adresses, interruptions et seuils sont propres à chaque SoC. Ils ont été lus dans le DT du
`vendor_boot` d'HyperOS (`OS2.0.204.0.VLFMIXM`), dont la sauvegarde avait été faite avant le flash.

```sh
mkdir -p /tmp/vb && cp vendor_boot.img /tmp/vb/ && cd /tmp/vb
unpack_bootimg --boot_img vendor_boot.img --out out      # paquet mkbootimg-aosp
fdtdump out/dtb 2>/dev/null > out/all.txt                # un seul DT : « Cape LTE Only SoC »
grep -n -i "bwmon" out/all.txt
```

Le fichier `dtb` contenait un seul device tree (modèle `Cape LTE Only SoC`, `qcom,gpu-model =
"Adreno730v3"`), donc le bon. Nœuds trouvés :

| Nœud Android | Registres | Interruption | Rôle |
|---|---|---|---|
| `qcom,bwmon-llcc@190b6400` (`bwmon4`) | `0x190b6400` (taille `0x300`) + global `0x190b6300` | SPI `0x245` = **581** | CPU vers LLCC |
| `qcom,bwmon-ddr@19091000` (`bwmon5`) | `0x19091000` (taille `0x1000`) | SPI `0x51` = **81** | LLCC vers DDR |

Recoupement avec `sm8550.dtsi` : mêmes interruptions (581 et 81) et mêmes adresses à la différence du
préfixe (`0x240b6400` et `0x24091000`). C'est ce qui permet de réutiliser les compatibles SM8550. Les
nœuds du SM8550 utilisent `reg = <0 0x240b6400 0 0x600>`, repris ici avec `0x190b6400`.

Sur un SoC à `#interrupt-cells = 3` (SM8450 et dérivés), l'interruption s'écrit
`<GIC_SPI 581 IRQ_TYPE_LEVEL_HIGH>` (le SM8550 en a 4).

### Les seuils

Les nœuds `bwmon` pointent (`qcom,target-dev`) vers deux nœuds `dcvs-hw` d'Android :

| `dcvs-hw` | `qcom,bus-width` | Table de fréquences |
|---|---|---|
| `llcc` (phandle `0x4d`) | 16 | `llcc-freq-table` (phandle `0x48`) |
| `ddr` (phandle `0x4b`) | 4 | `ddr-freq-table` (phandle `0x45`) |

Fréquences lues (en kHz) :

- DDR : 547000, 768000, 1555000, 1708000, 2092000, 2736000, 3196000
- LLCC : 300000, 466000, 600000, 806000, 933000, 1066000

Conversion en `opp-peak-kBps`, convention du noyau (vérifiée contre `sm8550.dtsi`) :

```
kBps = fréquence(kHz) x largeur de bus / 1,048576   (arrondi au millier inférieur)
```

```sh
python3 cpu/scripts/calculer-seuils-bwmon.py
```

| Table | Valeurs |
|---|---|
| `cpu_bwmon_opp_table` (CPU vers LLCC, llcc x 16) | 4577000, 7110000, 9155000, 12298000, 14236000, 16265000 |
| `llcc_bwmon_opp_table` (LLCC vers DDR, ddr x 4) | 2086000, 2929000, 5931000, 6515000, 7980000, 10437000, 12191000 |

La table CPU est identique à celle de `sm8550.dtsi` (même LLCC). Pour la DDR, les six premiers paliers
sont communs avec le SM8550 ; le dernier (`12191000`, soit 3196 MHz) est propre au SM8475.

Interconnexions utilisées (les mêmes que `sm8550.dtsi`, macros présentes dans `qcom,sm8450.h`) :

- CPU bwmon : `gem_noc MASTER_APPSS_PROC` vers `gem_noc SLAVE_LLCC`, tag `QCOM_ICC_TAG_ACTIVE_ONLY` ;
- LLCC bwmon : `mc_virt MASTER_LLCC` vers `mc_virt SLAVE_EBI1`, tag `QCOM_ICC_TAG_ACTIVE_ONLY`.

## 4. Tester sans reconstruire le noyau (surcouche + `fdtoverlay`)

À faire dans l'arbre du noyau (pour `-I include`), sur le téléphone. La surcouche utilise les phandles
numériques de `gem_noc` (`0x46`) et `mc_virt` (`0x30`) du DTB installé : les vérifier d'abord
(commandes en § 3 de [gpu/README.md](../gpu/README.md)).

```sh
curl -L -o /tmp/bwmon.dtso https://raw.githubusercontent.com/rakhenmanoa/alarm-xiaomi-diting/main/extra/bwmon-overlay/bwmon.dtso
cpp -nostdinc -I include -undef -x assembler-with-cpp /tmp/bwmon.dtso | grep -v '^#' > /tmp/bwmon.dts
dtc -q -W no-reg_format -W no-avoid_default_addr_size -W no-unit_address_vs_reg -I dts -O dtb -o /tmp/bwmon.dtbo /tmp/bwmon.dts
fdtoverlay -i /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb -o /tmp/diting-bwmon.dtb /tmp/bwmon.dtbo
dtc -q -I dtb -O dts /tmp/diting-bwmon.dtb 2>/dev/null | grep -c "bwmon\|opp-peak-kBps"
```

Le dernier nombre, avec le GPU déjà corrigé, doit être **39** (11 autres `opp-peak-kBps` du DTB, 13 du
GPU, 13 des nouvelles tables, 2 lignes `compatible` des `bwmon`).

Pas de `-@` à la compilation : il ajouterait un nœud `__symbols__` au DTB. Les avertissements
`reg_format` sont normaux dans une surcouche (le parent n'est pas connu à la compilation).

Installation (avec sauvegarde) :

```sh
sudo cp /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb.avant-bwmon
sudo install -m644 /tmp/diting-bwmon.dtb /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb
sudo mkinitcpio -P
sudo diting-install-bootimg
sudo reboot
```

Retour arrière : remettre `sm8475-xiaomi-diting.dtb.avant-bwmon`, puis `mkinitcpio -P` et
`diting-install-bootimg`.

**Risque** : une adresse de registres fausse peut figer le démarrage. Ici elles viennent du DT d'Android
et se recoupent avec le SM8550, mais rien ne remplace le test.

## 5. Vérifier que ça marche

```sh
lsmod | grep -i bwmon                       # icc_bwmon chargé
ls /sys/bus/platform/devices | grep -E "190b6400|19091000"
ls /sys/bus/platform/drivers/qcom-bwmon/    # 190b6400.pmu et 19091000.pmu liés
```

Le pilote s'appelle `qcom-bwmon` (et non `icc-bwmon`). Puis, en root, avec un trafic mémoire forcé :

```sh
sudo watch -n 0.5 'grep -i -A3 "^ebi@\|pmu" /sys/kernel/debug/interconnect/interconnect_summary'
dd if=/dev/zero of=/dev/null bs=64M count=300      # dans une seconde session
```

Résultat du 2026-11-13 : les deux moniteurs sont liés, `190b6400` et `19091000` montent sous charge,
puis redescendent au repos. Au repos, `19091000` reste à son palier minimal (`2086000`) : c'est son état
normal, pas un défaut (une grande partie du trafic reste dans le cache LLCC et n'atteint pas la DDR).

## 6. Résultat sur la fluidité

Pas de différence nette ressentie (Firefox, Plasma). Ce n'est pas surprenant : les mesures montrent que le
GPU n'est pas saturé dans Firefox (5 %) et que les cœurs montent bien en fréquence, donc la mémoire n'était
pas le goulot. Les deux correctifs fonctionnent, mais leur effet est faible ; ils coûtent un peu de
batterie en échange. D'où le choix de les documenter ici sans en faire une condition de l'image.

Pistes qui restent plus prometteuses : voir § 6 de [gpu/README.md](../gpu/README.md) (nombre de pixels
rendus, chemin d'affichage de KWin).

## 7. Pièges

- **Un seul `dtb` dans le `vendor_boot`** de cette sauvegarde, mais en cas de plusieurs, repérer le bon par
  `model = "…Cape…"` et `qcom,gpu-model = "Adreno730v3"`.
- **Ne pas copier le DTB compilé dans `linux-7.2.10/`** : cet arbre n'a pas tous les patches du dépôt
  (voir § 5 de [gpu/README.md](../gpu/README.md)). La surcouche évite ce problème.
- **Les mises à jour du paquet `linux-diting` écrasent le DTB de `/boot`** : pour que les correctifs
  survivent, il faut que le `PKGBUILD` applique `0926` et `0927`, puis reconstruire le paquet.
- Le message `disp_cc_mdss_mdp_clk_src: rcg didn't update its configuration` au démarrage est celui du
  pilote d'horloge de l'écran ; sans rapport avec les `bwmon`.

## 8. Capacité des cœurs et modèle d'énergie (patch `0928`)

### Constat

Sur le téléphone, avant le patch :

```sh
grep . /sys/devices/system/cpu/cpu*/cpu_capacity
```

donnait **1024 pour les 8 cœurs**. Le noyau voyait donc huit cœurs identiques, alors que le SM8475 a :

| Cœurs | Type | Fréquence max |
|---|---|---|
| cpu0 à cpu3 | Cortex-A510 (petits) | ~2,0 GHz |
| cpu4 à cpu6 | Cortex-A710 (moyens) | ~2,75 GHz |
| cpu7 | Cortex-X2 (gros) | ~3,2 GHz |

Conséquences : pas de placement des tâches selon la puissance du cœur, modèle d'énergie (EAS) impossible,
et montée en fréquence calculée sans tenir compte de l'écart entre cœurs. `sm8450.dtsi` ne décrit ni
`capacity-dmips-mhz` ni `dynamic-power-coefficient` pour ses cœurs.

### Les valeurs, lues dans le DT d'Android

Dans le `vendor_boot` d'HyperOS (voir § 3), nœuds `cpu@0` à `cpu@700` :

```sh
cd /tmp/vb && grep -n "cpu@\|capacity-dmips-mhz\|dynamic-power-coefficient" out/all.txt | head -60
```

| Cœurs | `capacity-dmips-mhz` | `dynamic-power-coefficient` |
|---|---|---|
| `cpu@0`, `@100`, `@200`, `@300` | `0x400` = **1024** | `0x64` = **100** |
| `cpu@400`, `@500`, `@600` | `0x8cd` = **2253** | `0x101` = **257** |
| `cpu@700` | `0x952` = **2386** | `0x1fd` = **509** |

Le patch `0928` les écrit pour `&cpu0` à `&cpu7` dans `sm8475.dtsi` (les labels `cpu0` à `cpu7`
existent dans `sm8450.dtsi`). Le coefficient est en µW/MHz/V², l'unité attendue par le noyau.

### Prérequis du noyau (vérifiés sur le téléphone)

```sh
cat /sys/devices/system/cpu/cpufreq/policy*/scaling_governor     # schedutil (x3)
zcat /proc/config.gz | grep -E "CONFIG_ENERGY_MODEL|CONFIG_CPU_FREQ_GOV_SCHEDUTIL|CONFIG_ARM_QCOM_CPUFREQ_HW|CONFIG_SCHED_MC"
```

Résultat : gouverneur `schedutil` sur les trois groupes, `CONFIG_ENERGY_MODEL=y`,
`CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL=y`, `CONFIG_ARM_QCOM_CPUFREQ_HW=y`, `CONFIG_SCHED_MC=y`. Il ne
manquait que la description des cœurs.

### Test sans reconstruire (`fdtput`)

Fait sur le DTB installé, après sauvegarde. Les nœuds sont `/cpus/cpu@0` … `/cpus/cpu@700` :

```sh
sudo cp /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb.avant-cpucap
sudo fdtput -t u /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /cpus/cpu@0 capacity-dmips-mhz 1024
sudo fdtput -t u /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /cpus/cpu@0 dynamic-power-coefficient 100
# … idem pour @100, @200, @300 (1024 / 100), @400, @500, @600 (2253 / 257), @700 (2386 / 509)
sudo mkinitcpio -P
sudo diting-install-bootimg
sudo reboot
```

Pour relire les valeurs, une paire « nœud propriété » par appel (`fdtget` n'accepte pas deux propriétés
pour un même nœud en une seule fois) :

```sh
fdtget -t u /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /cpus/cpu@400 capacity-dmips-mhz
fdtget -t u /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /cpus/cpu@400 dynamic-power-coefficient
```

Retour arrière : remettre `sm8475-xiaomi-diting.dtb.avant-cpucap`, puis `mkinitcpio -P` et
`diting-install-bootimg`.

### Résultat (2026-11-13)

Après redémarrage :

| Cœurs | `cpu_capacity` |
|---|---|
| cpu0 à cpu3 | **277** |
| cpu4 à cpu6 | **832** |
| cpu7 | **1024** |

Le modèle d'énergie est enregistré (`sudo ls /sys/kernel/debug/energy_model/` → `cpu0`, `cpu4`, `cpu7`)
et `cat /proc/sys/kernel/sched_energy_aware` donne `1`.

**Effet sur la fluidité : à confirmer à l'usage.** Les capacités et le modèle d'énergie sont actifs ;
le ressenti (interface de Plasma, Firefox) et la consommation n'ont pas encore été évalués au moment
de cette écriture. À noter ici une fois jugés.

### Pièges

- Le commutateur `sched_energy_aware` à `1` indique que l'EAS est autorisé, pas qu'il est actif à tout
  instant : il ne joue que lorsque le système n'est pas saturé.
- Les `dynamic-power-coefficient` d'Android sont ceux de son propre ordonnanceur ; ils conviennent comme
  point de départ mais n'ont pas été recalibrés pour Linux mainline.
- Comme pour le GPU et les `bwmon`, ces valeurs ne survivent à une mise à jour du paquet `linux-diting`
  que si le `PKGBUILD` applique `0928` (c'est le cas depuis `pkgrel 35`).

## Voir aussi

- [gpu/README.md](../gpu/README.md) : la tension du GPU, la table de fréquences, le patch `0926`.
- [screen/README.md](../screen/README.md) : l'affichage et la fluidité.

## 9. L3, LLCC et DDR votés par les CPU (patch 0929)

Le `sm8450.dtsi` ne donne aucune table OPP aux cœurs : `cpufreq-hw` règle
seulement l'horloge du cœur, le L3, le LLCC et la DDR restent au plancher.
Le patch 0929 déclare le fournisseur `epss_l3` (0x17d90000) et trois tables
OPP (16, 20 et 21 niveaux) dont `opp-peak-kBps` = (LLCC×16) (DDR×4) (L3×32),
reprises par rang du `sm8550.dtsi` (DDR plafonnée à 3196000).

- Nécessite `CONFIG_INTERCONNECT_QCOM_OSM_L3=y` (module `=m` : `cpufreq-hw`,
  intégré, ne peut pas attendre le fournisseur). Fait dans le `PKGBUILD`
  (pkgrel 36).
- Validé sur le téléphone (paquet 7.2.10-36) : `interconnect_summary` montre
  `epss_l3` avec les clients cpu0, cpu4 et cpu7 (un par grappe). Au repos
  (cpu7 à 1996800 kHz) : L3 40550400, LLCC 9600000, DDR 6832000 ; sous charge
  (3187200 kHz) : L3 54067200, LLCC 14928000, DDR 12784000. Les votes GPU
  (0926) sont inchangés.
- Piège : `INTERCONNECT_QCOM_OSM_L3=m` ne convient pas, il doit être `=y`.
- Wild Life (3DMark, via Waydroid) plante avec un « gpu fault » du noyau, mais
  c'était déjà le cas avant ces patchs : bug du pilote Vulkan de l'image
  Waydroid, sans lien. Sling Shot Vulkan passe sans problème.
