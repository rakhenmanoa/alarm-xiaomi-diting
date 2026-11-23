# ISP matériel (ICP / BPS / IPE) — état des lieux

Retour au [README principal](../README.md) · [scoreboard](../scoreboard.md).

Aujourd'hui libcamera traite les images brutes sur le CPU (ISP logiciel). Le diting a
pourtant un vrai ISP matériel hors ligne : un cœur de contrôle **ICP** (Xtensa LX7) qui fait
tourner un firmware, et deux moteurs, **BPS** (Bayer) et **IPE** (post-traitement).
Cette page note ce qu'on sait, pour reprendre le travail quand un pilote sera utilisable.

## État upstream (16 nov. 2026)

- **ICP + BPS + IPE** : RFC « very loose » de Bryan O'Donoghue (Linaro, 3 juil. 2026, v1),
  pour le x1e80100 seulement. Elle démarre l'ICP et échange en HFI, mais ne traite aucune
  trame (l'envoi de trame du BPS est désactivé par un `#if 0`, la destruction d'un handle
  échoue). Rien de fusionné.
- **OPE** (autre moteur, puces d'entrée de gamme) : série de Loïc Poulain, v5 le
  24 juil. 2026, famille Agatti. Fonctionne, mais ne concerne pas le SM8475.

Il n'existe donc pas encore de pilote à adapter « tel quel » : ce qui suit prépare le terrain.

## Ce qu'on a vérifié sur le diting

Source : device tree Android (`vendor_boot_a.img`, bloc « Cape SoC »), lu avec
`tools/dt-inventaire.py`, et le firmware du `vendor` de la sauvegarde HyperOS.

| Élément | diting (SM8475) | RFC (x1e80100) |
|---|---|---|
| Cœur ICP | `qcom,cam-lx7`, 1 BPS, 1 IPE | ICP |
| Registres ICP | `lx7_csr` 0xac01000 (0x400), `lx7_cirq` 0xac01800 (0x400), `lx7_wd0` 0xac04000 (0x1000) | ICP à 0xac01000 |
| BPS | `qcom,cam-bps680` à 0xac2c000 (0x7800) | BPS à 0xac2c000 |
| IPE | `qcom,cam-ipe680` à 0xac42000 (0x16000) | non détaillé |
| Interruption ICP | SPI 463, nom `lx7` | IRQ index 0 |
| CPAS | 0xac13000, caméra NoC 0xac19000, RPMh 0xbbf0000, SPI 459 | — |
| Interconnects | `cam_ahb`, `cam_hf_0`, `cam_sf_0`, `cam_sf_icp` | `ahb`, `hf_0`, `sf_0`, `sf_icp` |
| Firmware | `CAMERA_ICP` (`.mdt` + segments `.b00`…`.b21`), chargé par PIL (`icp_use_pil`) | `CAMERA_ICP.mdt`, chargé via `qcom_scm_pas_auth_and_reset` |
| Taille du firmware | 0x700000 (7 Mo, segments du `.elf` et région IOMMU `fw_uncached`) | 7 340 032 octets |
| Zone réservée du firmware | `camera_region@9f500000`, 0x800000 (8 Mo), `no-map` : le firmware (7 Mo) y tient | zone de 8 Mo à 0x8e100000 |

Les adresses de l'ICP et du BPS, les noms d'interconnects et la taille du firmware sont
identiques. Les horloges ont les mêmes fonctions avec des noms légèrement différents
(BPS : `bps_ahb`, `bps_fast_ahb`, `bps`, `cpas_bps` ; IPE : `ipe_nps_ahb`,
`ipe_nps_fast_ahb`, `ipe_pps_fast_ahb`, `ipe_nps`, `ipe_pps`, `cpas_ipe_nps`).

Flux IOMMU de l'ICP (SID, masque 0x420 ou 0x400) : `0x2000 0x2020 0x2040 0x2060 0x2080
0x2100 0x2120 0x2400 0x2420 0x2440 0x2460 0x2480 0x2500 0x2520`.

## Ce qui reste inconnu

1. ~~`pas-id`~~ **Trouvé : 33**, comme le x1e80100. Le pilote caméra Android
   (`drivers/cam_icp/icp_hw/lx7_hw/lx7_core.h`, `CAM_FW_PAS_ID 33`) charge le firmware avec
   `qcom_mdt_load()` puis `qcom_scm_pas_auth_and_reset(33)`, exactement comme la RFC.
2. **Mémoire HFI** : la RFC demande une deuxième `memory-region` (pool `shared-dma-pool`
   d'environ 272 Mo). Android n'en déclare pas de dédiée, il alloue dynamiquement. Il faudra
   en créer une dans le DTS, et vérifier que `camera_region@9f500000` n'est pas déjà
   déclarée autrement dans `sm8450.dtsi`.
3. **Compatibilité du HFI** : même taille et même chargeur ne prouvent pas que le firmware du
   SM8475 parle le même protocole que celui du x1e80100. Indice favorable : la chaîne de
   version du firmware du diting (`CICP.FW.5.0.0006_TZ_ABORT`, segment `.b08`) est de la même
   branche 5.0 que celui de la RFC (`CICP.FW.5.0-00020`, compilé le 8 fév. 2023), avec le même
   format de ligne de journal (`...,BUILD_TIME: ...,CACHE_ENABLED`). Le numéro de build
   diffère, et le suffixe `_TZ_ABORT` est inexpliqué. Seul un démarrage réel tranchera.
4. **Numéros d'horloges** (`camcc`, `gcc`) du DT Android à rapprocher des identifiants du
   `camcc` du noyau, et GDSC `titan_top`, `bps`, `ipe_0` correspondants.

## Sources disponibles

- **RFC Linaro (3 juil. 2026)** : incomplète. Le patch 3 n'ajoute que `camss-icp.c`,
  `camss-icp.h` et `camss-icp-hfi.h`. Il manque `camss-icp-hfi.c` (files de messages,
  `SYS_INIT`, lecture des messages), `camss-bps-cmd.[ch]`, `camss-bps-iq.h` et
  `camss-icp-hfi-frame.h`. Le pilote ne peut donc pas être compilé tel quel.
- **Pilote Android de référence (GPL, fork Xiaomi du camera-kernel SM8450)** :
  `github.com/xiaomi-sm8450-kernel/android_vendor_qcom_opensource_camera-kernel`,
  branche `zeus-s-oss` : `drivers/cam_icp/hfi.c` (958 lignes), `icp_hw/lx7_hw/`,
  `fw_inc/hfi_intf.h`. C'est la base pour écrire `camss-icp-hfi.c`.
- Le dépôt `MiCode/Xiaomi_Kernel_OpenSource`, branche `diting-s-oss`, ne contient que des
  device trees et des bindings, pas le pilote caméra.

## Prochaines étapes

1. Écrire `camss-icp-hfi.c` à partir du `hfi.c` Android et de `camss-icp-hfi.h` de la RFC.
2. Écrire les nœuds `icp`, `bps`, `ipe` dans le DTS du diting (désactivés par défaut).
3. Reprendre la RFC sur le noyau du dépôt, avec les valeurs du diting, et tenter un
   démarrage de l'ICP (échange HFI) sans traiter d'image.
4. Seulement après : traitement d'une trame par le BPS, puis intégration libcamera.

Les fichiers du firmware (`CAMERA_ICP.*`) sont propriétaires : ils ne sont pas dans le dépôt.

## État du portage

**16 nov. 2026 : l'ICP démarre sur le diting.** Le firmware `CICP.FW.5.0.0006` répond à la
demande d'initialisation, puis à `SYS_INIT` et au ping (`HFI ready, fw version 0x01000100`,
version d'API `0x23200ba`). Aucune image n'est encore traitée : il n'y a ni BPS ni IPE.

- `0934` : pilote ICP de la RFC complété par `camss-icp-hfi.c` (écrit à partir du `hfi.c`
  Android) et cas `qcom,sm8475-camss-icp`. Option `VIDEO_QCOM_CAMSS_HARD_ISP`, module
  `qcom-camss-icp`. Le paramètre `icp_stage=1..5` arrête le démarrage à une étape donnée.
- `0935` : nœud `icp@ac01000` dans `sm8475.dtsi`, **désactivé** par défaut.
- `extra/camss-icp/` : compiler le module seul, et `faire-overlay.py` pour ajouter le nœud
  au `.dtb` en cours sans recompiler le noyau.

Ce qui était faux dans la première version et faisait redémarrer le téléphone à la demande
d'initialisation (les deux points ont été corrigés ensemble, on ne sait pas lequel suffit) :

- **Registres CIRQ** : sur sm8450, état à `0x0c`, acquittement à `0x04` puis commande à
  `0x10`, ICP2HOST sur le bit 2 et chien de garde sur le bit 4 (`lx7_reg.h` Android). La RFC
  x1e80100 utilise `0x04`/`0x08` et les bits 0 et 6. Les interruptions restent masquées
  jusqu'à la réponse du firmware.
- **Flux SMMU** : les 14 flux du contexte « icp » du device tree Android
  (`0x2000 0x420`, `0x2040 0x420`, `0x2100 0x420`, `0x2080 0x400`) et `dma-coherent`.

Confirmé par l'essai : `pas_id` 33, zone `camera_mem@9f500000`, horloges et domaines du
sm8450, adresses HFI sous 256 Mo.

Le firmware doit être copié depuis la sauvegarde Android (`CAMERA_ICP.*`, y compris
le `.mdt`) dans `/usr/lib/firmware/qcom/sm8475/xiaomi/diting/`.

**Sessions BPS et IPE.** Avec `icp_selftest=1`, le module ouvre puis ferme une session
BPS et une session IPE dans le firmware : les quatre opérations réussissent. La structure
`hfi_cmd_async` de la RFC avait un champ `fw_handle` en trop après l'opcode (absent du
`hfi_cmd_ipebps_async` Android), d'où l'échec de destruction signalé dans la RFC.

**Redémarrage sans reboot.** `rmmod` puis `insmod` fonctionne : les registres de handshake
(GP1 à GP3) sont remis à zéro avant chaque démarrage, sinon le firmware retrouve
l'ancienne demande d'initialisation et ne répond plus.

**Configuration des entrées et sorties (`CONFIG_IO`).** Acceptée par le firmware pour le
BPS (Bayer 10 bits vers NV12, 1920×1080) et pour l'IPE (NV12 vers NV12). Les structures de
l'en-tête du noyau Android sont périmées pour ce firmware : la description d'image fait
96 octets (60 côté Android), `BpsConfigIoData` a 12 images (1240 octets) et
`IpeConfigIoData` 62 images (8336 octets).

**Le firmware décrit ses propres structures.** `CAMERA_ICP.elf` contient ses tables de
débogage (DWARF). `tools/camera/dwarf_struct.py <elf> <Structure>` et
`tools/camera/dwarf_enum.py <elf> <ÉNUMÉRATEUR>` en affichent la disposition exacte, par
exemple `_BpsFrameProcessData` ou `_IpeConfigIoData`. Ses messages d'erreur sortent dans
`dmesg` (lignes `ICP:`). Le firmware n'est pas dans le dépôt.

**Traitement d'une image (`FRAME_PROCESS`) : le point d'arrêt actuel.** Avec
`icp_selftest_frame=1`, le module envoie au BPS une image Bayer synthétique. Le firmware
lit la commande et répond : `Striping Output from host is empty while SL is absent in FW`.
Ce firmware ne contient donc pas la bibliothèque de découpage en bandes du BPS : c'est à
l'hôte de fournir `BpsStripingLibOutMultiFrame` (4 736 octets, plus 832 octets par bande).
Sous Android, ce bloc est calculé par la bibliothèque propriétaire de CamX.

Ce qu'une image demande au BPS, d'après le DWARF du firmware :

| Bloc | Structure | Taille | État |
|---|---|---|---|
| Données de trame | `BpsFrameProcessData` | 4 932 | écrit, accepté jusqu'au découpage |
| Tampon CDM | (travail du firmware) | 512 Kio | alloué, taille donnée par `CONFIG_IO` |
| Réglages d'image | `BpsIQSettings` | 1 276 | forme connue, contenu à zéro pour l'instant |
| Programmes de registres | `CDMProgramArray` | 32 + 24 par programme | forme connue, liste vide |
| Découpage en bandes | `BpsStripingLibOutMultiFrame` | 4 736 + 832 par bande | **manquant** |

**16 nov. 2026 : le BPS écrit dans l'image de sortie.** Avec le découpage calculé à la main
(une passe, une bande sur toute l'image), le firmware programme la lecture, les recadrages
et les moteurs d'écriture, et le matériel écrit 464 000 octets dans le tampon de sortie
avant de s'arrêter sur une violation matérielle (`SCHED: Violation occurred`, code
`0xa0009`). L'image n'est donc pas encore traitée jusqu'au bout, et son contenu est à zéro :
les réglages d'image (`BpsIQSettings`) sont presque tous à zéro.

Ce qu'on sait du bloc de découpage `BpsStripingLibOutMultiFrame`, lu dans le firmware :

- les plages de lignes (passe) et de pixels (bande) sont des paires (première, dernière) :
  paire 0 pour l'entrée, paire n + 1 pour la sortie de passe n (`BpsPassOutputType`). La
  sortie pleine résolution (`BPS_OUTPUT_FULL_DS_4` = 1) est donc la paire 2 ;
- `stripesArrayOffset` se compte depuis le début de la passe ;
- les recadrages (`cropPass`, `cropStripe`) doivent être activés : le firmware divise
  lui-même la largeur par deux pour la chrominance ;
- activer un module dans `BpsIQSettings` (champ `EN`) se retrouve dans son registre ;
- le tampon CDM est rangé à positions fixes : trame à 0, modules de la passe à 1 000,
  lecture et écriture de la passe à 8 000, bandes à 10 000. Après une erreur, le firmware
  le remplace par un rapport de diagnostic (étiquettes `0x1234xxxx`).

Outils : `tools/camera/cdm-decode.py` (commandes de registres), `dwarf_funcs.py` et
`dwarf_params.py` (fonctions et paramètres du firmware), `xtensa-dis.py` (désassemblage
partiel : les instructions larges FLIX de 8 octets ne sont pas décodées).

**23 nov. 2026 : première image traitée de bout en bout par le BPS.** En 960 × 480, avec le
dématriçage coupé (`icp_selftest_w=960 icp_selftest_h=480
icp_selftest_iq_patch=664,1,780,255,788,255`), le firmware répond `0x0` et les deux plans NV12
sont écrits en entier (luminance 480 lignes, chrominance 240). Le contenu n'a pas encore de sens
(réglages d'image presque tous à zéro), mais toute la chaîne fonctionne.

Ce qui a mené là :

- **Moteurs d'écriture.** Avec `icp_fw_dump=0`, le firmware n'écrase plus le tampon CDM par son
  rapport, et `cdm-decode.py` montre le programme de registres réel. Client 0 (luminance) à
  `0x1000`, client 1 (chrominance) à `0x1100` : `+0x04` adresse, `+0x0c` largeur | hauteur << 16,
  `+0x14` pas de ligne, `+0x18` empaquetage. Leur hauteur vient de la paire de lignes de sortie
  de la passe ; la première ligne de la paire est ignorée.
- **`WrStatus`** (décodage du pilote Android de l'IFE, même famille de bus) : bit 31 violation de
  taille d'image, bit 28 violation de contrainte (hauteur impaire en NV12), bit 0 probablement le
  client 0. `0x101` en fin de travail réussi.
- **Recadrages.** Sur les 16 recadrages du découpage, trois sont programmés : n° 1 → `0x5860`
  (sortie DS4 probable), n° 4 → `0x6060` (sortie pleine, luminance), n° 5 → `0x6260` (sortie
  pleine, chrominance, largeur divisée par deux par le firmware). Les autres seuls donnent un
  délai dépassé (`0xa0007`). Le second champ de ces modules (lignes) reprend la plage de pixels
  de la passe, pas sa plage de lignes.
- **Largeur maximale d'une bande : environ 1 000 pixels.** À 1 088 pixels et plus, la luminance se
  bloque dès la 2e ligne ; à 1 024, délai dépassé ; à 960, elle passe. Une image de 1 920 pixels
  demande donc au moins deux bandes.
- **Le dématriçage cause la violation restante.** Activé, la luminance signale une violation de
  taille quelle que soit la hauteur annoncée (476 à 480). Coupé, le travail aboutit. Hypothèse :
  il lui faut des marges aux bords de la bande (pixels et lignes en plus à l'entrée, retirés par
  les recadrages), que CamX calcule dans son découpage et que le nôtre ne prévoit pas.

Prochaines étapes : marges du dématriçage, découpage en deux bandes pour 1 920 pixels, puis
réglages d'image (balance des blancs, matrice de couleurs, conversion YUV) pour une image utile.

Reste à faire : BPS et IPE (les fichiers `camss-bps-cmd.*` de la RFC manquent), puis le
branchement sur libcamera.
