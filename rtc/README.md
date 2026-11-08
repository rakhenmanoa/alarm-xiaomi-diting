# Horloge (RTC) du Xiaomi 12T Pro (diting) — tout ce qui a été fait

Ce dossier regroupe tout ce qui concerne l'horloge matérielle du diting sous Arch Linux ARM :
le RTC du PMIC, pourquoi il est en lecture seule, et comment l'heure est gardée sans réseau.

> **Ce sont des copies de référence, figées le 2026-11-08.** Les fichiers réellement construits
> et installés restent ceux de `pkgbuilds/` (`linux-diting`, `diting-rtc`).
> Après une modification, mettez d'abord ces originaux à jour, puis recopiez ici.

## Contenu

| Dossier | Contenu | Original |
|---|---|---|
| `pilote/rtc-pm8xxx.c` | Pilote RTC des PMIC Qualcomm, tel quel dans Linux 7.2.10 (non modifié) | `drivers/rtc/rtc-pm8xxx.c` |
| `patches/diting-ubuntu/` | Activation du nœud `pmk8350_rtc` dans le device tree (royka1/diting-ubuntu, commit `c68ff47db947`) | `kernel/patches/0028` de diting-ubuntu |
| `scripts/diting-rtc/` | Démon `diting-rtcd`, service systemd, règle udev, PKGBUILD | `pkgbuilds/diting-rtc/` |
| `outils/uefivar-test.c` | Test en lecture seule de l'appli TrustZone `uefisecapp` (variables UEFI) | — |

## 1. Matériel

Le RTC est un bloc du PMIC **PMK8350** (SID 0, registres `0x6100` pour le compteur, `0x6200`
pour l'alarme), décrit dans `arch/arm64/boot/dts/qcom/pmk8350.dtsi` :

```dts
pmk8350_rtc: rtc@6100 {
	compatible = "qcom,pmk8350-rtc";
	reg = <0x6100>, <0x6200>;
	reg-names = "rtc", "alarm";
	interrupts = <PMK8350_SID 0x62 0x1 IRQ_TYPE_EDGE_RISING>;
	status = "disabled";
};
```

C'est un simple compteur de secondes sur 32 bits, alimenté en permanence par la batterie :
il continue de tourner téléphone éteint. Il ne repart de zéro que si la batterie est
complètement vidée.

## 2. Pilote du noyau : `rtc-pm8xxx`

Le pilote est celui de Linux, sans modification (`CONFIG_RTC_DRV_PM8XXX=m` dans le `defconfig`
arm64). Le patch `0028` de diting-ubuntu active simplement le nœud :

```dts
/* Read-only RTC (Android keeps the offset in userspace); wake alarms */
&pmk8350_rtc {
	status = "okay";
};
```

Il apparaît comme `/dev/rtc0`. Ce qui fonctionne :

- **lecture** du compteur (`cat /sys/class/rtc/rtc0/since_epoch`) ;
- **alarmes de réveil** : `rtcwake`, minuteries `WakeSystem=` de systemd, réveils de KClock
  (`CLOCK_REALTIME_ALARM`). Ils fonctionnent en durée relative (« dans N secondes »), donc
  indépendamment de l'heure affichée. Vérifié par diting-ubuntu (tests de veille).

Ce qui ne fonctionne pas, volontairement : **la mise à l'heure**. Sans la propriété
`allow-set-time` dans le device tree, le pilote refuse d'écrire le compteur (`hwclock -w`
renvoie `ENODEV`).

### Pourquoi en lecture seule

Sur les téléphones Qualcomm, le compteur du PMIC n'est jamais remis à l'heure, même par Android.
Android garde à part un **décalage** « heure réelle − compteur » (le `time_daemon` du vendeur,
fichiers sur `/data`). Écrire le compteur depuis Linux décalerait donc l'heure d'Android, qu'on
veut pouvoir retrouver intacte.

### Ce que propose le noyau, et pourquoi ce n'est pas utilisé ici

Depuis Linux 6.3, `rtc-pm8xxx` sait garder ce décalage lui-même, à deux endroits possibles :

| Méthode | Propriété DT | Utilisée par | Sur le diting |
|---|---|---|---|
| Cellule nvmem (registre SDAM du PMIC) | `nvmem-cells = <&…>; nvmem-cell-names = "offset";` | quelques cartes Qualcomm | **non** : on ne sait pas quels registres SDAM sont libres ; le bootloader, la charge et Android en utilisent. En écraser un au hasard est risqué |
| Variable UEFI | `qcom,uefi-rtc-info` | Lenovo ThinkPad X13s et portables Snapdragon | **non** : le diting démarre par l'ABL d'Android, sans UEFI ni variables EFI |

Le décalage est donc gardé hors du noyau, comme sous Android.

### Le piège `hctosys`

Avec `CONFIG_RTC_HCTOSYS=y` (par défaut), le noyau règle l'heure système sur le RTC au moment
où le pilote s'enregistre. Comme `rtc-pm8xxx` est un module, cela arrive **pendant** le
démarrage, après que systemd a déjà fixé une heure : l'heure saute alors à la valeur brute du
compteur (une date sans rapport, le compteur ne connaît pas l'heure réelle).

L'option est gardée, car le même code du noyau mesure aussi la durée de la veille par le RTC.
Le démon attend simplement que ce réglage soit fait (fichier `/sys/class/rtc/rtc0/hctosys` à `1`)
avant d'appliquer le décalage, et le réapplique s'il arrive malgré tout après lui.

## 3. Démon `diting-rtcd` (paquet `diting-rtc`)

Équivalent du `time_daemon` d'Android, en ~300 lignes de C sans dépendance
(`scripts/diting-rtc/diting-rtcd.c`). Il ne touche jamais au PMIC.

**Fichier d'état** : `/var/lib/diting-rtc/offset`, une ligne texte
`<décalage en s> <valeur du compteur au moment de l'écriture>`.

**Au démarrage** :

1. La règle udev `60-diting-rtc.rules` démarre `diting-rtcd.service` dès que `/dev/rtc0`
   apparaît (chargement du module).
2. Le démon attend la fin de `hctosys`, lit le compteur, et règle l'heure à
   `compteur + décalage`.
3. Si le compteur est **inférieur** à celui enregistré, le RTC a été remis à zéro (batterie
   vide) : le décalage n'est plus valable, il n'est pas appliqué et l'heure reste celle de
   `systemd-timesyncd` (dernière heure connue, puis NTP).
4. Il prévient systemd (`Type=notify`) : le service est ordonné **avant**
   `systemd-timesyncd` et `time-set.target`.

**Pendant le fonctionnement**, un `timerfd` sur `CLOCK_REALTIME` avec `TFD_TIMER_CANCEL_ON_SET`
signale chaque changement d'heure :

- heure changée (synchronisation NTP, réglage manuel dans Plasma, `timedatectl set-time`) :
  nouveau décalage enregistré aussitôt ;
- toutes les heures, si l'horloge est synchronisée par NTP (`adjtimex`) : le décalage est
  recalculé pour suivre la dérive du quartz du PMIC, et réécrit seulement s'il a bougé d'au
  moins 2 s (pour ménager la mémoire flash) ;
- à l'arrêt (`SIGTERM`, service arrêté avant `shutdown.target` alors que `/var` est encore
  monté) : dernier enregistrement.

**Précision** : le compteur est à la seconde, donc l'heure restaurée est juste à ±1 s près,
puis corrigée finement par NTP dès qu'il y a du réseau.

Le service tourne avec `CAP_SYS_TIME` seulement, accès à `/dev/rtc0` en lecture seule, système
de fichiers en lecture seule sauf `/var/lib/diting-rtc`, sans réseau.

## 4. Installation et vérification

Le paquet est construit par `scripts/20-construire-paquets.sh` et installé avec les autres
paquets `*-diting`. Sur un téléphone déjà installé, une commande à la fois :

```sh
sudo pacman -U /var/lib/diting-repo/diting-rtc-*.pkg.tar.*
```

puis redémarrer, et :

```sh
journalctl -b -u diting-rtcd
```

- Premier démarrage : « pas de décalage enregistré ». Dès que NTP a mis l'heure (Wi-Fi ou
  données) : « heure changée : décalage … s ».
- Démarrages suivants : « heure restaurée depuis le RTC : … ».

Test sans réseau : couper Wi-Fi et données (mode avion), redémarrer, puis

```sh
date
```

L'heure doit être juste dès l'écran de verrouillage.

Autres commandes utiles :

```sh
cat /sys/class/rtc/rtc0/since_epoch     # compteur brut du PMIC
cat /var/lib/diting-rtc/offset          # décalage enregistré
timedatectl                             # état NTP
```

## 5. Reste à faire

- Test sur le téléphone (état ⬜ dans le [scoreboard](../scoreboard.md)).
- Possible plus tard : retrouver dans le device tree d'Android un registre SDAM libre et passer
  au décalage géré par le noyau (`nvmem-cells`), ce qui rendrait `hwclock` utilisable. À ne faire
  qu'avec la certitude que ce registre n'est utilisé par personne.

## 6. Piste : garder le décalage en TrustZone (variable UEFI `RTCInfo`)

Sur les PC Snapdragon, le noyau garde le décalage dans la variable UEFI `RTCInfo`
(GUID `882f8c2b-9646-435f-8de5-f208ff80c1bd`, propriété DT `qcom,uefi-rtc-info`). Ces variables
sont gérées par l'appli TrustZone `qcom.tz.uefisecapp`, qui les range dans la partition
`uefivarstore`. Le diting a les deux (`uefisecapp_a/b`, `uefivarstore`).

État constaté le 2026-11-08 (voir les résultats plus bas) :

- `qcom_scm` trouve QSEECOM (version `0x1402000`) mais refuse de créer l'interface du noyau :
  `qseecom: untested machine, skipping` (liste blanche de machines, uniquement des PC) ;
- le pilote TEE `qseecom` du patch `0918`, lui, n'est pas soumis à cette liste et peut ouvrir
  une session sur une appli chargée par le bootloader.

`outils/uefivar-test.c` utilise ce pilote TEE pour interroger `uefisecapp` **en lecture seule**
(espace disponible, présence de `RTCInfo`, liste des variables), sans toucher au noyau :

```sh
cc -O2 -Wall -o uefivar-test rtc/outils/uefivar-test.c
sudo ./uefivar-test
sudo ./uefivar-test -l
```

Résultats sur le téléphone (2026-11-08) :

- `uefisecapp` **répond** depuis Linux : 64 512 o d'espace, 63 996 o libres ;
- **`RTCInfo` existe déjà** (créée par le bootloader) : attributs `0x7`, 12 octets, tout à zéro
  (décalage 0, Android ne s'en sert pas). Taille conforme à `struct qcom_uefi_rtc_info` ;
- 16 variables en tout (`RTCInfo`, `RunCycles`, `UefiSecAppId`, variables d'affichage, console).

Pour tester l'écriture sans toucher à `RTCInfo`, `uefivar-test -w` écrit une variable à part,
`DitingTest` (GUID `d17b6a3e-5c2f-4f0e-9a41-7d3c2b8e6f10`, contenant l'heure d'écriture), et
`-d` l'efface.

On peut aussi tester directement sur `RTCInfo` : `uefivar-test -r` y écrit le vrai décalage
(heure système − compteur de `/dev/rtc0`, converti en origine GPS comme le fait `rtc-pm8xxx`),
en gardant les 8 octets réservés ; `-z` remet la valeur d'origine (zéro). L'heure système doit
être synchronisée (`timedatectl`) avant `-r`.

Points à vérifier avant d'aller plus loin :

1. ✅ `uefisecapp` répond depuis Linux.
2. ✅ `RTCInfo` existe. (Sinon, `rtc-pm8xxx` avec `qcom,uefi-rtc-info` **échoue au
   démarrage** (lecture de la variable obligatoire) : plus de RTC du tout. Il faudrait la créer
   d'abord, ou patcher le pilote pour accepter son absence.)
3. ❌ **Une écriture ne survit pas au redémarrage.** Testé le 2026-11-08 : `uefivar-test -r`
   écrit `RTCInfo` (statut `0x0`, relecture juste : « compteur + décalage » = heure exacte),
   mais après redémarrage la variable est revenue à zéro. `uefisecapp` garde l'écriture en
   mémoire et ne la reporte pas dans `uefivarstore`. Sous Android, l'écriture sur le stockage
   passe probablement par un service (« listener ») de `qseecomd`, absent sous Linux. Sur téléphone, l'écriture dans `uefivarstore`
   pourrait demander un service (« listener ») côté Linux que seul Android fournit.
4. (Abandonné tant que le point 3 n'est pas résolu) patch du noyau ajoutant `xiaomi,diting` à
   `qcom_scm_qseecom_allowlist`, puis `qcom,uefi-rtc-info` sur `&pmk8350_rtc`.

Sauvegarder `uefivarstore` avant toute écriture :

```sh
sudo dd if=/dev/disk/by-partlabel/uefivarstore of=uefivarstore.img
```

**Conclusion** : la piste TrustZone est arrêtée. Le décalage reste gardé par `diting-rtc`
(fichier `/var/lib/diting-rtc/offset`). La rouvrir demanderait d'identifier et de servir depuis
Linux le listener de stockage qu'utilise `uefisecapp` (le pilote TEE `0918` sait servir des
listeners par le supplicant TEE), sans garantie que ce soit le seul obstacle.
