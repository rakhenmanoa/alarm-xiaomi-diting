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
| `patches/alarm-diting/0924-…` | Inscription des listeners QSEECOM à la manière d'Android (variante smcinvoke) | `pkgbuilds/linux-diting/0924` |
| `patches/diting-ubuntu/` | Activation du nœud `pmk8350_rtc` dans le device tree (royka1/diting-ubuntu, commit `c68ff47db947`) | `kernel/patches/0028` de diting-ubuntu |
| `scripts/diting-rtc/` | Démon `diting-rtcd`, service systemd, règle udev, PKGBUILD | `pkgbuilds/diting-rtc/` |
| `scripts/diting-qseecomd/` | Listener RPMB pour la TrustZone (variables UEFI persistantes) | `pkgbuilds/diting-qseecomd/` |
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
   mémoire. Sous Android, l'écriture sur le stockage passe par un service (« listener ») de
   `qseecomd`, absent sous Linux : voir section 7.
4. Une fois le point 3 résolu : patch du noyau ajoutant `xiaomi,diting` à
   `qcom_scm_qseecom_allowlist`, puis `qcom,uefi-rtc-info` sur `&pmk8350_rtc`.

Sauvegarder `uefivarstore` avant toute écriture :

```sh
sudo dd if=/dev/disk/by-partlabel/uefivarstore of=uefivarstore.img
```

## 7. Listener RPMB : `diting-qseecomd`

Les variables UEFI non volatiles sont enregistrées par `uefisecapp` dans le **RPMB** de l'UFS
(Replay Protected Memory Block : zone de la puce UFS où l'on n'écrit qu'avec une clé connue de
la seule TrustZone). La TrustZone n'accède pas elle-même à l'UFS : elle prépare des trames RPMB
chiffrées et signées, et demande au monde normal de les transmettre. C'est le listener
**RPMB, numéro `0x2000`**, servi sous Android par `qseecomd`. Sans lui, l'écriture reste dans
la mémoire de `uefisecapp`.

Qualcomm a publié ce service en open source (github.com/qualcomm/minkipc,
`listeners/librpmbservice`, BSD-3-Clause-Clear), pour son nouveau supplicant QTEE. Le protocole
(`TZ_CM_CMD_RPMB_INIT` `0x101`, `READ` `0x102`, `WRITE` `0x103`, `PARTITION` `0x104`) est celui
du `qseecomd` d'Android. Un patch de Qualcomm de juillet 2026 (Harshal Dev, « Add TEE based
client driver for uefisecapp ») confirme que la persistance des variables UEFI passe par ce
service RPMB.

`diting-qseecomd` (paquet `pkgbuilds/diting-qseecomd`) :

- reprend `rpmb.c` et `rpmb_ufs.c` de minkipc (accès au RPMB par `/dev/bsg/ufs-bsg0` et le LUN
  RPMB `/dev/bsg/0:0:0:49476`), sans le pilote eMMC, avec deux garde-fous contre une division
  par zéro ;
- sert le listener `0x2000` par le pilote TEE `qseecom` du patch `0918` : enregistrement par une
  session sur `/dev/teeprivN`, puis boucle `TEE_IOC_SUPPL_RECV` / `TEE_IOC_SUPPL_SEND` ;
- inscrit le listener avec un tampon de `0x7000` octets : la TrustZone du diting refuse une
  taille qui n'est pas un multiple de 4096 (`0x6400`, la valeur d'Android, est refusé avec
  `-EINVAL` ; 4096 à 32768 par pas de 4096 sont acceptés) ;
- recopie les trames de chaque requête avant de les envoyer (la réponse est écrite dans le même
  tampon) et vérifie que tout tient dans le tampon ;
- est **en lecture seule par défaut** : les écritures RPMB sont journalisées et refusées. `-w`
  les autorise. `-v` détaille chaque commande, `-e ID` enregistre un autre listener seulement
  pour journaliser ses demandes.

Ce programme ne voit que des trames chiffrées et signées : il ne peut ni lire ni forger le
contenu du RPMB, et une trame abîmée est refusée par la puce (MAC et compteur d'écritures).

Le service systemd (`diting-qseecomd.service`, avec `-w`) n'est **pas activé** par le paquet :
tester d'abord à la main.

### Test

Une commande à la fois. Construire et installer :

```sh
cd pkgbuilds/diting-qseecomd
makepkg -si
cd ../..
```

1. Lecture seule, en arrière-plan sous systemd :

   ```sh
   sudo systemd-run --unit=qseecomd-test diting-qseecomd -v
   sudo ./uefivar-test -r
   journalctl -u qseecomd-test
   ```

   On attend `RPMB init`, des lectures, puis `écriture … REFUSÉE`.

2. Avec écritures :

   ```sh
   sudo systemctl stop qseecomd-test
   sudo systemd-run --unit=qseecomd-test diting-qseecomd -v -w
   sudo ./uefivar-test -r
   journalctl -u qseecomd-test
   sudo reboot
   sudo ./uefivar-test
   ```

   Réussite : après le redémarrage, `RTCInfo` garde le décalage et « compteur + décalage »
   donne l'heure juste.

### Résultats (2026-11-08)

- Accès au RPMB depuis Linux : ✅ (`/dev/bsg/ufs-bsg0`, LUN RPMB `0:0:0:49476`, 32 768
  secteurs de 512 o soit 16 Mio, 64 trames par commande ; `CONFIG_RPMB` désactivé, aucun
  pilote du noyau ne le réclame).
- Inscription du listener `0x2000` : refusée avec un tampon de `0x6400`, acceptée avec `0x7000`
  (multiple de 4096 exigé).
- **Mais `uefisecapp` n'appelle jamais le listener** : avec `0x2000` inscrit, `SetVariable`
  sur `RTCInfo` répond tout de suite, sans aucune demande au listener, et la valeur reste en
  mémoire. Le noyau n'a pas non plus signalé de demande pour un listener absent lors des
  écritures précédentes.

### Piste suivante : l'inscription « smcinvoke » (patch `0924`)

Le pilote `qseecom` d'Android (msm, `QSEOS_REGISTER_LISTENER`) inscrit les listeners avec la
commande **`0x06`** (`TZ_OS_REGISTER_LISTENER_SMCINVOKE_ID`) et ne se rabat sur l'ancienne
**`0x01`** (`TZ_OS_REGISTER_LISTENER_ID`) que si la TrustZone ne la connaît pas. Le pilote du
patch `0918` n'utilise que `0x01`. Sur une TrustZone qui gère smcinvoke, comme celle du SM8475,
un listener inscrit à l'ancienne n'est peut-être visible que des applis qui appellent leurs
listeners à l'ancienne, ce qui expliquerait le silence de `uefisecapp`.

Le patch `0924` (`pkgbuilds/linux-diting`, copie dans `patches/alarm-diting/`) inscrit d'abord
avec `0x06`, puis avec `0x01` si la TrustZone refuse, et journalise la méthode retenue :
`qseecom: listener 8192 registered (smcinvoke)`.

Test après installation du noyau :

```sh
sudo systemd-run --unit=qseecomd-test diting-qseecomd -v
sudo dmesg | grep -i "qseecom: listener"
sudo ./uefivar-test -r
journalctl -u qseecomd-test
sudo systemctl stop qseecomd-test
```

Si la TrustZone appelle enfin le listener, le journal montre `RPMB init` et des lectures, puis
l'écriture refusée (lecture seule). On passe alors à `-w`.

Limite connue : si l'appli répond par une demande de rappel smcinvoke
(`QSEOS_RESULT_CBACK_REQUEST`) plutôt que par une demande de listener classique, le pilote
`0918` ne sait pas la traiter et l'interface QSEECOM peut rester bloquée jusqu'au redémarrage
(empreintes comprises). Rien n'est écrit dans ce cas.

### Résultat avec le patch `0924` (2026-11-08)

- Inscription : `qseecom: listener 8192 registered (smcinvoke)` : la TrustZone accepte la
  commande `0x06`.
- `diting-qseecomd` tourne, listener `0x2000` inscrit, puis `uefivar-test -r` près d'une heure
  plus tard : `SetVariable` répond succès **sans aucune demande au listener**, comme avec
  l'inscription à l'ancienne.

**Conclusion : `uefisecapp` n'écrit pas dans le RPMB pendant que Linux (ou Android) tourne.**
Il garde les variables non volatiles en mémoire ; seul le bootloader, qui a son propre accès
à l'UFS, les enregistre. Cela rejoint ce qu'écrit Qualcomm (série « Add TEE based client driver
for UEFI Secure Application », juillet 2026) : avec le RPMB sur UFS, l'interface QSEECOM de
`uefisecapp` ne permet pas de rendre ces variables persistantes ; il faut une version récente
de la TrustZone avec un `uefisecapp` en object-IPC, absente de ce firmware de 2022. Android ne
s'en sert d'ailleurs jamais (`RTCInfo` à zéro).

**La piste TrustZone est close.** Ce qui reste utile :

- `diting-qseecomd` fonctionne (accès RPMB, inscription, boucle de service) et pourra servir si
  une autre appli QSEE demande un jour le RPMB ; il n'est ni installé par défaut ni activé ;
- le patch `0924` inscrit les listeners comme Android, sans effet visible ici.

En attendant, le décalage reste gardé par `diting-rtc` (fichier `/var/lib/diting-rtc/offset`).
