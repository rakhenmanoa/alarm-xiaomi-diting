# Lecteur d'empreintes (étude de faisabilité)

Capteur Goodix optique sous l'écran. Rien ne passe par Linux : le bus SPI du
capteur appartient à la TrustZone, et une appli sécurisée Goodix fait la
capture, l'enregistrement et la comparaison. Android lui parle depuis son HAL
(`libgf_hal.so`, `fingerprint.goodix_fod.so`) par l'interface QSEECOM.

## Ce qu'on sait (2026-11-03)

| Point | Constat |
|---|---|
| DT Android (`fingerprint_goodix`) | reset GPIO 41, interruption GPIO 40, alimentations L1C et L3C ; aucun bus SPI côté Linux |
| Appli sécurisée | `gfdit.mdt` + `gfdit.b00`…`b08` dans la partition modem (`NON-HLOS.bin`, dossier `image/`), 15 Mo une fois assemblée |
| HAL Android | `vendor/lib64/libgf_hal.so` : `QSEECom_start_app`, `QSEECom_send_cmd`, classes `goodix::CaEntry`, `goodix::IonMemory` (zone carveout ION) |
| Interface TrustZone sous Linux | `qcomtee` actif, `/dev/tee0` ; l'ancienne `qseecom` est désactivée (« untested machine ») |
| Test [quic-teec](https://github.com/qualcomm/quic-teec) `-d` | OK : la TrustZone répond (service 143) |
| Chargement de `soter64` (85 Ko, appli de production) | **chargée** (contrôleur obtenu) ; `getAppObject` refusé : normal pour une appli QSEECOM |
| Chargement de `smplap64` (appli d'exemple) | refusé (`result 1`) |
| Chargement de `gfdit` | sans patch : impossible (plus de 4 Mo par tampon, `MAX_INBOUND_BUFFER_SIZE`, et mémoire partagée limitée à 4 Mo d'un bloc, `alloc_pages_exact`) ; **avec le patch 0001 (2026-11-04) : chargée** en mode 1, comme `soter64` |

Les applis se rassemblent en un `.mbn` avec `tools/mdt-squash.py` de
diting-ubuntu. Le test de quic-teec (`build/tests/qcomteetest -l DOSSIER 1 99`)
attend le fichier sous le nom `smcinvoke_skeleton_ta64.mbn` ; la commande 99,
inconnue, arrête le test juste après le chargement.

## Patch 0001 : grosses zones de mémoire partagée

Au-delà de 4 Mo, le pilote `qcomtee` prend la mémoire dans la réserve CMA
(`dma_alloc_pages`). Module à compiler sur le téléphone :

```sh
cd ~/linux-7.2.10
patch -Np1 < ~/alarm-xiaomi-diting/extra/empreintes/0001-tee-qcomtee-Allocate-large-shared-buffers-from-CMA.patch
make LOCALVERSION= KBUILD_MODPOST_WARN=1 drivers/tee/qcomtee/qcomtee.ko
sudo install -Dm644 -t /usr/lib/modules/$(uname -r)/updates/ drivers/tee/qcomtee/qcomtee.ko
sudo depmod -a
sudo reboot
```

## Suite

1. ~~Charger `gfdit`~~ : fait.
2. Envoyer une commande « à l'ancienne » (tampon requête/réponse) à une appli
   chargée par IAppLoader, comme le fait QSEECom sur les plateformes récentes.
3. Retrouver le protocole Goodix dans `libgf_hal.so`.
4. Allumer la zone du doigt (local HBM, commandes de la dalle) pendant la capture.

## Route QSEECOM (2026-11-04)

`gfdit` est une appli QSEECOM : elle répond à des commandes (tampon requête +
réponse), pas à des objets. Elle n'est donc pas joignable par `qcomtee`. Le
pilote QSEECOM de Dawid Wróbel et Sam Day ([samcday/linux, branche
`wip/sargo-fingerprint`](https://github.com/samcday/linux/pull/14)), rétroporté
dans le paquet `linux-diting` (patch `0918`, option `TEE_QSEECOM`), charge une
appli par son nom et lui transmet des commandes. Le patch CMA ci-dessus est
dans le paquet sous le n° `0919`.

Comme `QCOM_SCM` est intégré au noyau, il faut **reconstruire et installer le
paquet `linux-diting`** (pas seulement un module).

Le pilote cherche l'appli dans le dossier des firmwares : copier
`gfdit.mdt` et `gfdit.b00` à `gfdit.b08` (de `NON-HLOS.bin`, dossier
`image/`) dans `/usr/lib/firmware/`. Ces fichiers sont signés Xiaomi : ne pas
les publier.

`qseecom-cli.c` charge l'appli, ouvre une session et envoie des requêtes
brutes :

```sh
cc -O2 -Wall -o qseecom-cli qseecom-cli.c
sudo modprobe qseecomtee
sudo ./qseecom-cli gfdit
```

### Premier essai (2026-11-04) : réussi

Paquet `linux-diting` pkgrel 23 (patches 0918 et 0919), `gfdit.mdt` + `.b0x`
dans `/usr/lib/firmware/` :

```
$ sudo modprobe qseecomtee
$ sudo ./qseecom-cli gfdit
qseecom : /dev/teepriv0 (privilégié), /dev/tee1 (client)
chargement de gfdit
  open_session : ret=0x0 origine=0 session=1
session client sur gfdit
  open_session : ret=0x0 origine=0 session=1
appli chargée et session ouverte
```

`/dev/tee0` est `qcomtee`. L'appli Goodix démarre dans la TrustZone depuis
Linux, sans blocage. Suite : le protocole de `libgf_hal.so`
(`goodix::CaEntry::sendCommand`).

## Protocole Goodix (lu dans `libgf_hal.so`, 2026-11-04)

`goodix::CaEntry` (HAL d'Android) :

- `startTap` : `QSEECom_start_app("/vendor/firmware_mnt/image", "gfdit",
  4096)`, puis charge `keymaster64` pour en tirer une clé HMAC envoyée à
  `gfdit` (auth 1000 / commande 10, `GF_CMD_AUTH_SET_HMAC_KEY`) : sert aux
  jetons d'authentification d'Android, peut-être pas indispensable ici.
- `sendCommand(données, taille)` : `QSEECom_send_modified_cmd`, requête et
  réponse de 64 octets dans le même tampon, plus deux tampons ION dont le
  noyau écrit l'adresse physique (4 octets) dans la requête :
  - offset 0 : tampon de données (tas `qcom,qseecom`), taille en [8] ;
  - offset 32 : tampon sécurisé de 4 Mo (tas `qcom,secure-cdsp`,
    `secure_carveout`), valeur associée en [40].
  Autres champs : [12] compteur, [16] horodatage en ms, [24] niveau de
  journal. Statut de l'appli en [28] de la réponse.
- Données : `[0]` résultat, `[4]` module, `[8]` commande, puis la charge
  propre à la commande.

| Module | Rôle | Commandes (tables `goodix::*CmdIdTable`) |
|---|---|---|
| 1000 | auth (bio) | 0 SETACTIVITY_GROUP, 1 AUTHENTICATE, 2 PRE_ENROLL, 3 ENROLL, 4 POST_ENROLL, 5 GET_ID, 6 REMOVE, 7 ENUMERATE, 8 CANCEL, 9 SAVE_TEMPLATES, 10 SET_HMAC_KEY, 12 POST_AUTHENTICATE, 13 INIT_FINISHED |
| 1001 | algo | 0 INIT (180 octets), 1 ENROLL, 2 AUTHENTICATE, 3 POST_AUTHENTICATE, … |
| 1002 | capteur | 0 PRE_DETECT, 1 DETECT_SENSOR, 2 INIT, 3 CAPTURE_IMG, 4 DATA, 5 GET_IRQ_TYPE, 6 SET_CONFIG, 8 SLEEP, 9 WAKEUP, 11 STOP_CAPTURE, 13 RESET, 14 SET_EXPO |
| 1003 | test/usine | 7 GET_VERSION (664 octets, versions à partir de [28]), 5 FIND_SENSOR, 16 GET_SENSOR_INFO, … |
| 1004 | dump | 0 DUMP_DATA |

Côté matériel, le HAL passe par `/dev/goodix_fp` (pilote noyau Goodix
classique, ioctl `'g'`) : 0x6702 reset, 0x6707/0x6708 alimentation,
0x40046705/0x6706 horloge SPI, interruptions par netlink. Sous Linux il faudra
l'équivalent : alimentations L1C et L3C, reset GPIO 41, interruption GPIO 40.

Premier essai proposé, sans toucher au capteur :

```sh
sudo ./qseecom-cli gfdit --goodix 1003 7 664
```

### Essais du 2026-11-04

- `--goodix 1003 7 664` sans tampon sécurisé : l'appli répond, données
  `[12] = 1006` (`GF_ERROR_MEMORY_COPY_FAILED`) ; son journal reste vide.
- `--goodix-carveout 1003 7 664` (tampon **ordinaire** de 4 Mo en [32],
  taille en [40]) : `[12] = 0`, plus d'erreur ; champs de version vides
  (probablement remplis après l'initialisation). L'appli accepte donc de la
  mémoire non sécurisée à la place du tas `qcom,secure-cdsp`.
- Tailles relevées : `FingerprintCore::enumerate` envoie 1000/7 sur 112
  octets ; `CustomizedAlgo::init` 1001/0 sur 180 octets ; `getVersion`
  1003/7 sur 664 octets.
- Clé HMAC factice (1000/10), puis 1001/0, 1000/7, 1003/7 avec le tampon de
  4 Mo : tout répond 0, mais rien n'est écrit nulle part (données, tampon,
  réponse, journal).

### Lecture de l'appli `gfdit` (2026-11-04)

Le binaire garde sa table de symboles (≈ 3 600 noms), ce qui permet de lire
`tz_app_cmd_handler` (0xc970) sans deviner :

- La requête est bien `[0]` adresse des données, `[8]` taille, `[16]` horodatage,
  `[24]` niveau de journal, `[32]`/`[40]` tampon de 4 Mo (facultatif).
- **Le statut est écrit dans la requête**, à `[28]`, pas dans la réponse.
  Notre pilote recopie la requête dans de la mémoire TZ et ne la rendait pas :
  le statut était perdu, d'où les « 0 » muets. → patch noyau **0920** : la
  requête est rendue quand l'appelant la passe en `MEMREF_INOUT` (adresses
  physiques effacées avant). `qseecom-cli` la passe ainsi et affiche
  `[28]`, préréglé à `0xffffffff`.
- Avant toute commande, l'appli teste un drapeau posé par `tz_app_init` quand
  `gf_modules_start` réussit (démarrage des modules auth, algo, capteur,
  test usine, dump, heartbeat). **Si ce démarrage a échoué, chaque commande
  revient sans rien écrire.** Le témoin `0xffffffff` intact à `[28]` le dira.
- Essai : statut **1012** avec le tampon de 4 Mo, alors que sans lui la commande
  s'exécute (erreur 1006). Le tampon de 4 Mo tombait à une adresse non alignée
  sur une page (il suit les données dans la mémoire du noyau) ; `qseecom-cli`
  arrondit maintenant les données à la page.
- Sinon : enregistrement du tampon de données (`qsee_register_shared_buffer`),
  échec → statut 1012 ; puis aiguillage par module (`[4]`), résultat en
  `données[12]` et journal recopié après les données.

### Journal de QSEE (patch 0921)

Le statut est maintenant **1006** (`GF_ERROR_MEMORY_COPY_FAILED`) avec le
tampon de 4 Mo aligné : la commande s'exécute, mais l'erreur peut venir d'une
centaine d'endroits. Le binaire est une version « release » : son journal ne
revient jamais dans le tampon de données (`gf_acquire_log_data` est vide) ; il
passe uniquement par `qsee_log()`, que QSEE jette tant qu'aucun tampon de
journal n'est enregistré (Android le fait au démarrage).

Patch 0921 (retiré au pkgrel 26) : enregistrer un tampon de journal auprès
de QSEE (commande 6 de QSEE_OS/APP_MGR). TZ l'accepte, mais la mémoire devient
illisible pour Linux : toute lecture donne une « synchronous external abort ».
Les noyaux Android récents font de même quand le journal est **chiffré**
(`is_encrypted_log_enabled`) : le tampon en clair n'est plus pour nous, et le
journal chiffré ne se lit qu'avec la clé de Qualcomm. Voie abandonnée.

### Origine du 1006 de 1003/7 (lecture du code)

`1003/7` → `sz_product_test_get_version` (taille ≥ 0x298 = 664) →
`algo_factory_get_algo_version`, qui recopie des chaînes de version de l'objet
algo. L'une est copiée avec `strlen()` : **vide tant que l'algo n'est pas
initialisé**, d'où une copie de longueur 0 que `gf_memcpy` refuse → 1006.
Il faut donc envoyer `1001/0` (initialisation de l'algo) d'abord, dans la
même session.

### Ordre d'initialisation d'Android (`HalContext::init`)

Après `startTap` (chargement + clé HMAC) :

| Étape HAL | Commande | Taille |
|---|---|---|
| `Sensor::doWakeup` | 1002/9 | 32 |
| `SensorDetector::init` (avec reprises) | 1002/0 | 28 |
| `SensorDetector::detectSensor` | 1002/1 | 44 |
| `Sensor::init`, puis `SZAlgo::init` | …, 1001/0 | 180 |

`1001/0` seul fait planter l'appli (TrustZone répond -22 ensuite, même capteur
alimenté : L1C forcée, GPIO 41 à 1) : l'algo dépend de la détection du capteur.

### Premier dialogue complet (2026-11-04)

Conditions : L1C (1,8 V) forcée allumée (`regulator-always-on` ajouté par
`fdtput` sur `/soc@0/rsc@17a00000/regulators-1/ldo1`), GPIO 41 (reset) à 1
(`gpioset -z -c gpiochip6 41=1`), tampon de 4 Mo aligné, patch 0920.

```sh
sudo ./qseecom-cli gfdit --goodix-carveout 1002 9 32 1002 0 28 1002 1 44 1002 2 744 1001 0 180 1003 7 664
```

Tout répond 0 :

- `1002/9` (réveil) : `[28] = 1`.
- `1002/1` (détection) : `[32] = 8`, `[36] = 224`, `[40] = 192` (taille d'image
  probable 224 × 192).
- `1002/2` (init capteur) : configuration (seuils, drapeaux), avec une
  chaîne `GCFS18…` qui semble venir du capteur lui-même.
- `1001/0` (init algo) : passe, alors qu'il plantait l'appli (SCM -22) sans
  les commandes `1002` avant.
- `1003/7` : `Shenzhen_v_2.00.41.08.57`, `Preprocess_v_1.01.02`,
  `CDFake_v_2.53.20.26`, `V03.02.07.13`, `Shenzhen_MP_OLED_INNER_1.0.3.27.17`.

Contre-épreuve, GPIO 41 remis à 0 : `1002/0` → **1043**, `1002/1` → **1064**,
`1002/2` et `1001/0` → 1001. Le reset du capteur est donc indispensable, et
les réponses viennent bien du capteur. Le patch **0922** garde L1C allumée ;
le reset reste à l'espace utilisateur (le HAL le manipule) :

```sh
sudo gpioset -z -c gpiochip6 41=1
```

Suite : `1002/6` (type d'écran, 480 octets), la capture (point lumineux sous le
doigt, interruption sur GPIO 40), l'enrôlement et l'identification.

### Capture : ce que fait Android (lecture de `libgf_hal.so`)

Commandes utiles (module/commande, taille) :

| HAL | Commande | Taille |
|---|---|---|
| `Sensor::captureImage(op, arg)` | 1002/3, `[28]` = arg, `[32]` = op | 40 |
| `Sensor::getIrqEventType` | 1002/5 | 32 |
| `Sensor::sleepSensor` | 1002/8 | 28 |
| `CustomizedSensor::getDisplayType` | 1002/6 | 480 |
| `SZAlgo::enrollImage` | 1001/1 | 48 |
| `SZAlgo::authImage` | 1001/2 | 160 |
| `FingerprintCore::enroll` / `authenticate` | 1000/3 (108), 1000/1 (48) |  |
| `SZProductTest::localAreaSample` (usine) | 1003/38 | 20544 |

L'éclairage sous le doigt passe par `/sys/class/mi_display/disp-DSI-0/disp_param`
du noyau Xiaomi : `9 1` / `9 0` (HBM local), `5 1024` / `5 -1` (HBM global).
Il faudra l'équivalent dans notre pilote de dalle (commandes DSI « local HBM »
du DT Android).

`GF_DUMP=préfixe` fait écrire à `qseecom-cli` les données complètes de chaque
commande dans `préfixe-MODULE-CMD-SEQ.bin`.

### Codes d'erreur de `gfdit` (table de `gf_strerror`)

| Code | Nom |
|---|---|
| 1001 | GF_ERROR_OUT_OF_MEMORY |
| 1004 | GF_ERROR_BAD_PARAMS |
| 1006 | GF_ERROR_MEMORY_COPY_FAILED |
| 1012 | GF_ERROR_GENERIC |
| 1028 | GF_ERROR_SPI_RAW_DATA_CRC_FAILED |
| 1043 | GF_ERROR_CHIP_ID_NOT_CORRECT |
| 1064 | GF_ERROR_SENSOR_NOT_AVAILABLE |
| 1096 | GF_ERROR_TOO_FAST |
| 1155 | GF_ERROR_UI_DISAPPEAR |

Capture `1002/3` avec `[32] = 1` (comme `onEnrollDownEvt`) : **1028**, CRC
des données brutes faux. La lecture SPI de l'image a donc lieu ; reste à
savoir pourquoi les données sont abîmées (tampon de 4 Mo ordinaire au lieu du
tas sécurisé du CDSP ? pose non terminée ? écran ?).

### Où en est la capture (2026-11-04, fin de matinée)

`1002/3` renvoie toujours **1028** (CRC des données brutes faux), quels que
soient :

- le tampon de 4 Mo (avec ou sans) ;
- `1002/6` (type d'écran 5) envoyé avant ;
- le gouverneur CPU (`performance`) ;
- les horloges AHB du groupe QUP n°1 forcées allumées
  (`power/control = on` sur `ac0000.geniqup` et `a94000.spi`) ;
- `clk_ignore_unused pd_ignore_unused`.

L'image est lue en 21 morceaux, chacun suivi d'un CRC 15 bits du capteur
(`gf_sz_dc_capture_logo_img` → `gf_crc_rawdata`) : c'est l'intégrité du
transfert qui pèche. Paramètres SPI par défaut de l'appli
(`gf_init_qsee_params`) : bus **10** (SE10, broches 36-39), **25 MHz** en
lecture d'image, 4,8 MHz et 960 kHz pour les petites commandes. Linux lit
`gcc_qupv3_wrap1_s2_clk_src` à **6,4 MHz** après une capture : l'appli n'a
peut-être pas obtenu sa vitesse haute (horloge, parent ou vote de bande
passante du QUP côté Linux ?). DT Android : `fingerprint_goodix` n'a que
`l1c_vdd`, `l3c_vdd`, reset 41 et irq 40.

DT Android (`vendor_boot`) : `spi@a88000` (SE10) déclaré mais `disabled`
(propriété de TrustZone), `spi-max-frequency` = 50 MHz ; le groupe
`qupv3_1_geni_se@ac0000` vote lui-même `qup-core`. Essai avec le module
`qup1-vote/` (vote `qup-core` 200 Mo/s et `qup-config` 100 Mo/s tant qu'il est
chargé) : **toujours 1028**.

Pistes restantes : vérifier si l'appli renonce à la vitesse haute
(`gf_spi_set_high_speed`) ; observer le GPIO 40 pendant la capture.

### Lecture de `gf_sz_dc` (capture)

`gf_sz_dc_capture_image` → `sz_dc_capture_image_by_twoframe_mode` :
commande de départ au capteur (`sz_dc_capture_image_start_cmd`), puis
**attente calculée** (`frame_time`, `gf_sleep`) — pas d'interruption — puis
`gf_bus_dc_read_rawdata` → `gf_spi_read_ext(mode 8)` → `qsee_spi_full_duplex`
à la **vitesse haute (25 MHz)** ; les commandes de détection et d'init passent
en vitesse basse (4,8 MHz / 960 kHz). Taille des paquets et blancs de ligne
calculés pour 24,8 MHz « réels » (`gf_spi_get_actual_high_speed`), valeur
fixe, aucun appel ne la change. Configuration SPI : bits par mot 8, CS actif
haut, mode 0. Seule la lecture rapide échoue : piste d'un transfert à 25 MHz
(DMA du QUP vers la DDR ?) moins bien servi que sous Android. Le module
`qup1-vote` vote aussi `qup-memory` (QUP1 → DDR).

### DMA du groupe QUP n°1

Android donne à Linux les canaux GPI **0 à 5** de `qcom,gpi-dma@a00000`
(`qcom,gpii-mask = <63>`), notre DT les canaux 1 à 6 (`dma-channel-mask =
<0x7e>`). Essai avec `0x3f` (`fdtput`) : **toujours 1028**.

### Première image brute (2026-11-04)

Les captures d'usine passent : `1003/19` (mode usine, 252 octets à zéro, comme
`SZProductTest::factoryInit`) puis `1003/21` (**921 640 octets**, taille réelle
du HAL : `0xe1028`).

```sh
sudo GF_DUMP=$HOME/usine ./qseecom-cli gfdit --goodix-carveout 1002 9 32 1002 0 28 1002 1 44 1002 2 744 1002 6 480 1001 0 180 1003 19 252 1003 21 921640
./gf-image.py ~/usine-1003-21-*.bin trame
```

Réponse : `[28]` = 192 lignes, `[32]` = 224 colonnes, puis **5 trames** de
192 × 224 pixels 16 bits. Sans éclairage : valeurs 1150-1167 (moyenne ≈ 1159),
seulement un bruit de lignes ; aucune trace de corruption. **Le capteur et le
bus à 25 MHz fonctionnent** : le CRC faux de `1002/3` vient d'autre chose que
du transfert lui-même (mode « deux trames » de la capture normale ?).

### Temps de pose et images éclairées

`1003/18` (44 octets) règle la pose : `[28]` = temps de pose (16 bits, non nul
et ≤ un maximum du capteur — 500 refusé, 200 accepté), `[40]` = flottant non
nul (1,0 convient). Écran blanc à luminosité maximale, doigt posé :

| Pose | Moyenne | Max |
|---|---|---|
| défaut | ≈ 1160 | ≈ 1170 |
| 50 | ≈ 2085 | ≈ 3540 |
| 200 | ≈ 4110 | 5553 (saturé) |

Géométrie réelle : **192 de large × 224 de haut** (`[28]` = largeur,
`[32]` = hauteur). On voit la tache lumineuse du doigt et une grille régulière
de petits groupes de points (structure du capteur ou de la dalle). Les crêtes
restent à faire ressortir : soustraire une image de référence sans doigt,
même pose, et utiliser le vrai éclairage local.

Image avec doigt (pose 100) divisée par la référence sans doigt (même pose,
noir ≈ 1158 retranché), puis filtre passe-bande (périodes 6-22 px) : des
**crêtes orientées** apparaissent au centre, mais faibles et noyées dans le
moiré de la dalle (pics haute fréquence dans le spectre). Avec un écran blanc
ordinaire, la lumière ne suffit pas : il faudra le HBM local (disque très
lumineux sous le capteur, commandes DSI propres à Xiaomi).
