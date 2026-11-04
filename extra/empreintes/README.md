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
