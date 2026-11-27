# ISP matériel (ICP / BPS / IPE) — état des lieux

Retour au [README principal](../README.md) · [scoreboard](../scoreboard.md) · [récapitulatif et reprise](isp-reprise.md).

Aujourd'hui libcamera traite les images brutes sur le CPU (ISP logiciel). Le diting a
pourtant un vrai ISP matériel hors ligne : un cœur de contrôle **ICP** (Xtensa LX7) qui fait
tourner un firmware, et deux moteurs, **BPS** (Bayer) et **IPE** (post-traitement).
Cette page note ce qu'on sait du matériel ; le pilote V4L2 du BPS (utilisable depuis le
23 nov. 2026) est décrit dans [isp-pilote.md](isp-pilote.md).

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

**Après un démarrage à froid, réveiller la caméra avant de charger le module.** Le firmware démarre
(`HFI ready`) mais la première commande, `create handle`, expire (`-110`) tant que le sous-système
caméra n'a pas été alimenté. Une capture avec `cam` suffit : `cam -c 1 -s role=viewfinder -s
role=raw,width=1920,height=1080 --capture=5 --file=/tmp/reveil-#.raw`, puis `insmod`. Constaté le
25 nov. 2026 : sans elle, deux essais ont échoué, et le `rmmod` fait sur un firmware muet a
bloqué le téléphone (redémarrage forcé). Ne jamais faire `rmmod` après un `create handle: -110` :
redémarrer.

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

**Dématriçage : ce qui est mesuré (23 nov., 960 × 480).** Avec le dématriçage, la luminance
reçoit 4 lignes de moins que l'entrée lue, et la chrominance la moitié de l'entrée. Avec
l'entrée lue sur 484 lignes (paire 0 de la passe), la sortie sur 480 et la chrominance recadrée
sur ses lignes 1 à 240 (champ `firstOut/lastOut` du recadrage n° 5 de la passe, que le firmware
écrit dans `0x6268`), les deux plans ont la bonne taille, mais la violation reste. Elle reste
aussi avec une marge de 4 pixels en largeur, et sans le recadrage du DS4. La sortie en cause
n'est donc pas identifiée. Registres utiles : `0xa54` position de la bande (0 = bande unique,
1 = voisine à droite, 2 = voisine à gauche, 3 = les deux), `0xa5c` largeur lue en octets,
`0xa60` hauteur lue, `0x4960` activation du dématriçage.

**23 nov. 2026, soir : image traitée avec dématriçage, en 3 bandes.** En 960 × 480, avec le
découpage calculé par la bibliothèque de CamX (`sl-960x480.bin`, 3 bandes de 200, 332 et 444
pixels lus), le dématriçage et la conversion YUV actifs, le firmware répond `0x0`,
`WrStatus 0x101`, et les deux plans NV12 sont écrits en entier.

**23 nov. 2026 : pleine résolution.** En 1920 × 1080, 5 bandes (`sl-1920x1080.bin`),
dématriçage actif : `0x0`, `WrStatus 0x101`, luminance 1 080 lignes et chrominance 540 lignes
écrites en entier (3 110 400 octets). Commande :
`icp_selftest_w=1920 icp_selftest_h=1080 icp_selftest_sl_file=sl-1920x1080.bin
icp_selftest_iq_patch=492,1,664,1,780,255,788,255`.

**Lecture de l'image produite.** Après l'autotest, la sortie NV12 (sans lignes de réserve) est
dans `/sys/kernel/debug/qcom-camss-icp/bps-out.nv12` ; `ffmpeg -f rawvideo -pix_fmt nv12 -s
1920x1080 -i bps-out.nv12 bps-out.png` la convertit. Avec le dégradé d'entrée, le dématriçage
et sans conversion YUV (`icp_selftest_iq_patch=492,1,780,255,788,255`), la luminance reproduit
exactement le dégradé (0, 32, 64… 224 tous les 240 pixels), sans cassure entre les 5 bandes.
Avec la conversion YUV activée, tout vaut 0 : ses coefficients ne sont pas dans
`BpsIQSettings` mais dans les registres du module, que CamX programme par `CDMProgramArray`
(liste vide chez nous). Une entrée réelle se charge avec `icp_selftest_in_file=` (Bayer
10 bits dans des mots de 16 bits, depuis `/lib/firmware`).

**23 nov. 2026 : première vraie photo traitée par le BPS.** Image brute du HP1 capturée avec
`cam -c 1 -s role=viewfinder -s role=raw,width=1920,height=1080 --capture=30
--file=hp1-#.raw` (avec un flux brut seul, l'image est entièrement à zéro : l'exposition
automatique ne tourne pas ; avec l'aperçu, le capteur passe en 4096 × 3072, SGBRG10_CSI2P),
centre 1920 × 1080 extrait par `tools/camera/raw10-to-bps.py image.raw 1920 1080 hp1.bin 4096
3072`, chargé avec `icp_selftest_in_file=hp1.bin`, ordre Bayer GB (`icp_selftest_patch=12,3
icp_selftest_frame_patch=3684,3`). La luminance montre la scène, sans cassure entre les bandes ;
elle est sombre et plate (ni gamma, ni balance des blancs, ni conversion YUV programmés).

**23 nov. 2026 : première photo en couleur.** Les coefficients des modules ne sont pas dans
`BpsIQSettings` (qui ne porte que leurs interrupteurs) : CamX les écrit par un programme de
registres. `icp_selftest_regs=adresse,valeur,…` joint à l'image un programme `REG_RANDOM` de
type 72 (`PROGRAM_TYPE_GENERIC`, longueur codée « octets − 1 ») ; le firmware l'exécute.

Les registres viennent de `camera.qcom.so` (sauvegarde HyperOS, `vendor/lib64/hw`, pas dans le
dépôt) : sa section `.gnu_debugdata` (xz) contient la table des fonctions, dont les classes
`BPS…Titan680` ; `CreateCmdList` donne l'adresse et le nombre de registres,
`PackIQRegisterSetting` leur format, `Initialize` les valeurs par défaut, et les fonctions
`…Setting::CalculateHWSetting` l'échelle.

| Module | Interrupteur (`BpsIQSettings`) | Registres | Format |
|---|---|---|---|
| Conversion YUV (CST 1.2) | octet 664 | `0x5468`, 18 | par canal (Y, Cb, Cr), 6 registres : coeff. 0 et 1 (bits 0-12 et 16-28), coeff. 2 (bits 0-12), décalage ajouté (bits 19-31), 0, borne min (bits 20-31), borne max (bits 20-31). Coefficients signés 13 bits, 1,0 = 1 024, entrées dans l'ordre G, B, R. Domaine 12 bits : décalage de chrominance 2 048 pour 128 en sortie 8 bits. |
| Balance des blancs (WB 2.0.1) | octet 988 | `0x3868`, 12 | gains G, B, R aux registres 0 à 2, bits 17-31, 1,0 = 1 024 ; le reste à 0 |
| Matrice de couleurs (CC 1.3.1) | octet 652 | `0x4b68`, 10 | coefficients 12 bits signés, échelle 2^(7+q) (q aux bits 0-1 du registre 9), décalages bits 19-31 aux registres 6 à 8 |

Valeurs CST utilisées (BT.601 de CamX, décalages 2 048) :
`0x5468,0x750259,0x546c,0x132,0x5470,0x0,0x5474,0x0,0x5478,0x0,0x547c,0xfff00000,0x5480,0x1fe1eae,0x5484,0x1f54,0x5488,0x40000000,0x548c,0x0,0x5490,0x0,0x5494,0xfff00000,0x5498,0x1fad1e55,0x549c,0x1fe,0x54a0,0x40000000,0x54a4,0x0,0x54a8,0x0,0x54ac,0xfff00000`.
Avec la balance des blancs calculée en « monde gris » sur l'image (B × 1,38, R × 1,43),
la photo du HP1 sort avec des couleurs neutres (U et V moyens à 128), encore sombre : il
manque le gamma (module GLUT, table chargée par un programme DMI) et la matrice de couleurs.

**Gamma (GLUT, `BPSGamma151Titan680`).** Interrupteur à l'octet 660 de `BpsIQSettings`. Trois
tables de 256 entrées de 32 bits (1 Ko), chargées par trois commandes CDM DMI (mot 0 : `1 << 24
| (0x400 − 1)`, mot 1 : adresse de la table, mot 2 : `sélecteur (1, 2, 3) << 24 | 0x5208`),
dans un programme de type 7 (`BPS_GLUT_LUT_PROGRAM`). Entrée : valeur 12 bits (bits 0-11) et
écart signé avec l'entrée suivante (bits 12-23). `tools/camera/bps-gamma.py sortie.bin 2.2 1.0
64` écrit les tables (gamma 2,2, noir à 64 sur 10 bits retiré dans la table) ; chargement par
`icp_selftest_glut_file=`. Avec le gamma, la luminance moyenne de la photo passe de ~40 à 109 ;
avec le noir mesuré (67), les noirs redeviennent noirs. Le module de soustraction du noir
(`BPSBLS121Titan680`, 6 registres à `0x3e68`, interrupteur à l'octet 1060) : format décodé le
25 nov. 2026 (`tools/camera/bps-bls.py`) : niveau de noir à `0x3e68` (champ de 18 bits, bits
14-31), gain Q11 `2^N / (2^N - noir)` à `0x3e6c`, quatre seuils par canal à `0x3e70-0x3e7c`
(plafond `2^N - 1`). Vérifié sur le téléphone le 25 nov. 2026 : le décalage se donne à l'échelle N = 17 (noir 67 sur
10 bits -> 8 576 ; à N = 14 il est sans effet, à N = 18 deux fois trop fort). Les quatre « Threshold
config » sont des seuils de noir par canal, même échelle : sous le seuil, le canal est forcé à 0
(0x3e70 Gr, 0x3e74 Gb, 0x3e78 B, 0x3e7c R). Niveau de noir à retenir : **54** (sur 10 bits, `bps-bls.py 54`) : 0,0 % de pixels sombres colorés ;
58 en laisse 0,001 %, 62 0,062 % (taches rouges visibles). Seuils à laisser à 0x4000, sans effet : relever le seuil d'un canal (rouge 0x4a00 : taches rouges
remplacées par de grandes plaques turquoise) ou des quatre (0x4400 : 1,3 % de pixels sombres colorés,
0x4c00 : 7 %) ajoute du bruit coloré dans les ombres, parce que la coupure agit sur les échantillons
bruts avant le dématriçage. Mesure : part de pixels `Y < 70` avec `|U-128| > 25` ou `|V-128| > 25`.

**Matrice de couleurs.** `tools/camera/bps-ccm.py c0 … c8 [q]` convertit une matrice RVB 3x3
en registres CC 1.3.1 (interrupteur à l'octet 652). Les matrices du HP1 sont dans
`com.qti.tuned.diting_semco_s5khp1_wide.bin` (sauvegarde HyperOS, `vendor/lib64/camera`, pas
dans le dépôt) : une trentaine de matrices (selon la température de couleur et la luminosité),
repérables comme 9 flottants dont chaque ligne fait 1. Le découpage ne change pas avec elle.
Vérifié le 23 nov. 2026 : la matrice identité redonne l'image sans matrice, et la matrice
`1.74 -0.5794 -0.1605 -0.2432 1.4669 -0.2237 -0.0624 -0.7943 1.8567` du HP1 donne des couleurs
plus saturées, sans permutation. Chaîne complète utilisée : `icp_selftest_iq_patch=492,1,652,1,
660,1,664,1,780,255,788,255,988,1`, `icp_selftest_regs=` CST + WB + CC (40 écritures, limite
128), `icp_selftest_glut_file=` tables gamma avec noir à 67.

**Balance des blancs et noir : vérifications du 23 nov. 2026.** Ordre des gains G, B, R
confirmé : gains calculés sur le mur gris de la photo, il ressort neutre (U = V = 128) ; dans
l'ordre G, R, B, il vire (V = 118). Le module de balance des blancs multiplie les valeurs brutes,
noir compris : les gains doivent être calculés sur les valeurs avec le noir tant que le noir
n'est retiré qu'ensuite (dans la table gamma). Calculés sans le noir, ils laissent une dominante
(magenta ou cyan selon l'ordre). Le vrai remède sera le module de soustraction du noir, avant la
balance des blancs. La méthode « monde gris » se trompe sur une scène dominée par une couleur
(tissu orange rendu rose). Matrice de couleurs : le rangement des lignes est vérifié (ajouter
0,3 V à la sortie R donne au mur gris une teinte rouge, U 125, V 137) ; la matrice
`1.74 -0.5794 …` sature trop et tire l'orange vers le rose sous cet éclairage : CamX choisit et
interpole la matrice selon la température de couleur, ce qui reste à faire. Avec la matrice
identité, les couleurs sont proches de la réalité.

**Le découpage dépend des modules activés** : activer la balance des blancs change les marges
des bandes ; avec un découpage calculé sans elle, violation de taille. Recalculer le découpage
avec les mêmes `--iq` que l'image (`bps-striping.py … --iq 492,1,664,1,780,255,788,255,988,1`).

**Bibliothèque de découpage de CamX.** `tools/camera/bps-striping.py LIB.so LARGEUR HAUTEUR`
exécute `libipebpsstriping.so` (sauvegarde HyperOS, `vendor/lib64`, pas dans le dépôt) dans un
émulateur Unicorn : elle n'importe que la libc. Appels : `BPSStripingLibraryContextCreate(configIo,
memOp, titanVersion = 0x60800, hwVersion, &handle, &taille, &tailleCdm)` puis
`BPSStripingLibraryExecute(handle, {iq, cadl, cœurs, reConfigIo}, sortie, métadonnées,
userBPSArgs)`. Sa bande fait 848 octets ; le DWARF du firmware en décrit 832, mais le firmware lit bien
les bandes de 848 octets (avec 832, seule la première bande est lue correctement et la
lecture comme l'écriture sont coupées). Le fichier produit va dans `/lib/firmware` et se charge avec
`icp_selftest_sl_file=`. Ce qu'elle montre : 448 pixels de sortie au plus par bande (3 bandes
pour 960, 5 pour 1 920, rangées de droite à gauche), 4 pixels de marge par côté pour le
dématriçage, plages de lignes de la passe égales à l'image, recadrages de la passe en lignes
(chrominance : 0 à H/2 − 1).

**27 nov. 2026 : le vert était une copie du bleu (récupération de canal).** Avec la matrice du
fichier de réglage, l'orange virait au rose. Mesures sur la photo de test : l'image brute est bien
GBRG avec quatre positions distinctes (`tools/camera/raw10-phases.py`), mais en sortie du BPS le vert
était identique au bleu, pixel pour pixel, et le gain « bleu » de la balance des blancs faisait bouger
les deux. Cause, lue dans `BPSWB201Titan680::UpdateFirmwareData` (CamX) et le DWARF du firmware : la
balance des blancs 2.0 a une **récupération de canal** (reconstruction des canaux saturés) réglée par
`BpsIQSettings` octet 992 (`WbParameters.channelRecovery`) et 996 (`recoveryInterpRange`), que le
firmware programme lui-même à partir de `0x3860`. CamX y met 3 (coupée) en usage normal ; à 0 elle
est active avec des seuils nuls et remplace le vert. **Toujours passer `--iq 992,3`.** Écrire le
registre `0x3894` ne sert à rien (le firmware écrit par-dessus). Les registres du dématriçage
(`0x4968`-`0x4974`) et ses interrupteurs (octets 492-512 : `EN`, `COSITED_RGB_EN`, `DIR_G_INTERP_DIS`,
`DIR_RB_INTERP_DIS`, `DYN_G_CLAMP_EN`, `DYN_RB_CLAMP_EN`) n'y étaient pour rien. Une fois le vert
réel, les seuils du BLS à `0x4000` mettaient le vert des ombres à 0 (taches violettes) : seuils à 0
par défaut dans `bps-bls.py`. Ensuite, ombres magenta : le BLS agit **après** la balance des blancs, donc le noir multiplié
par les gains laissait un excès de rouge et de bleu. Les registres 3 à 8 de la balance des blancs
(`0x3874`-`0x3888`, champ de 18 bits aux bits 14-31, même valeur partout dans CamX) sont un noir
préservé : sortie = (entrée − noir) × gain + noir, échelle noir10 × 256 (`--wb-noir 54` dans
`bps-params.py`). Avec lui, les gains se calculent sur l'image brute sans le noir (1,71 et 1,91
pour la photo de test) et mur comme ombres sont neutres. Le noir lui-même a été mesuré sur une
image noire (objectif masqué) : **64** sur les quatre positions ; avec 54, les ombres gardaient un
reste magenta. Valeurs retenues : `--wb 1,1.80,2.04 --wb-noir 64` et `bps-bls.py 64`. Matrice selon l'éclairage :
`tools/camera/chromatix-cc13.py`.

**23 nov. 2026 : pilote V4L2 du BPS.** Le BPS est un périphérique mémoire à mémoire
(`camss-bps.c`, dans le module `qcom-camss-icp`) : RAW10 compacté en entrée, NV12 en sortie,
réglages d'image et découpage passés en un bloc (`tools/camera/bps-params.py`). La photo du HP1
sort par `/dev/video18` identique à celle de l'autotest, en ~4 ms. Architecture, format du bloc
et commandes d'essai : [isp-pilote.md](isp-pilote.md).

Reste à faire : découpage en pleine taille (4096 × 3072), gestionnaire libcamera `diting` et
son IPA (balance des blancs, exposition, choix de la matrice de couleurs, noir), module de
soustraction du noir (BLS), puis l'IPE et le JPEG.

## EEPROM de calibration du module HP1

Puya P24C128F (16 Ko) à l'adresse 0x51 du bus CCI du HP1 (`i2c-3`), déjà déclarée dans le DTS
et gérée par `at24` : `sudo cat /sys/bus/i2c/devices/3-0051/eeprom > ~/hp1-eeprom.bin`
(le capteur doit être alimenté : `echo on > /sys/bus/i2c/devices/3-0010/power/control`).
Décodage : `tools/camera/hp1-eeprom.py`. Le fichier brut reste hors du dépôt (numéro de série).

- Blocs « 00 id 01 … somme », somme = octets depuis 01 jusqu'avant la somme, modulo 255.
- Bloc 03 (0x60) : ombrage 17 × 13, R Gr Gb B en 16 bits gros-boutiste dès 0x67, centre
  normalisé à 1023. Chute aux coins : gain de correction ≈ 5,5 à 6,5 (R chute le plus vite).
- Bloc 04 (0x770) : balance des blancs, moyennes R Gr Gb B du module puis de la référence.
  Avec le noir 64 : gains du module R 1,62, B 1,80 (le gain B de 1,80 réglé à la main était juste).
- Bloc 07 (0xf30) : cartes de gain PDAF gauche et droite 17 × 13 (minimum décalé à droite
  pour la gauche et inversement), précédées des dimensions.
- Blocs 09 « PD01 » et 0b « PDXT » : données PDAF (défocalisation, diaphonie), à décoder.
- Bloc 02 (0x45) : 0x02a0, 0x025e, 0x03ff, peut-être les codes de mise au point du VCM.
- 0x59 répond aussi sur ce bus sans pilote : à identifier (OIS ?).

## Ombrage de l'objectif (LSC 4.1.1, `BPSLSC411Titan680`)

Relevé le 27 nov. 2026 dans `camera.qcom.so` (`CreateCmdList`, `PackIQRegisterSetting`,
`UpdateFirmwareData`, noms des champs dans les journaux de `LSC411CalculateSetting`) et dans le
DWARF du firmware (`LensRollOffParameters_480`, `_ProgramType`).

- **Tables** : 3 DMI de 884 octets (17 × 13 mots de 32 bits, ligne par ligne) au registre
  `0x3608`, sélecteurs 1, 2, 3, programme de type 5 (`BPS_LENS_ROLLOFF_LUT_PROGRAM`).
  Table 1 : R | Gr << 14 ; table 2 : B | Gb << 14 ; table 3 : ALSC (gain moyen 12 bits |
  moyenne 18 bits << 12), inutile sans ALSC. Gains sur 14 bits, 1,0 = 1 024.
- **Registres** (13 à `0x3668`) : 0 : gains horizontaux − 2 (bits 0-3), verticaux − 2 (4-7) ;
  1 : largeur de bloc − 1 (0-10), hauteur − 1 (16-25) ; 2 : largeur de sous-bloc − 1 (0-10),
  `x_delta` (12-28) ; 3 : hauteur de sous-bloc − 1 (0-9), `y_delta` (12-28) ; 4 : `Bx_d1`, `By_e1`
  (départ dans le sous-bloc) ; 5 : décalage ajouté (bits 14-31) ; 6 : `By_init_e1` (0-19) ;
  7, 8, 12 : pondération de luminance (ALSC) ; 9, 10 : recadrage ; 11 : décalage retranché
  (bits 14-31). Profondeur du module : 18 bits (noir 10 bits × 256).
- **`BpsIQSettings`, octets 444 à 488** : `EN`, `ALSC_EN`, `NUM_SUBBLOCKS` (facteur
  d'interpolation, 2^n sous-blocs par bloc), largeur de sous-bloc − 1, largeur de bloc − 1,
  bloc, sous-bloc et pixel de départ, gains horizontaux − 2, puis départs verticaux.
- Tout est dans le domaine Bayer (demi-largeur). La bibliothèque de découpage le confirme :
  position = x/2 + départs, bloc = position / largeur de bloc, sous-bloc et pixel ensuite. Le
  découpage doit être recalculé avec ces octets de `BpsIQSettings` (bandes calées sur les blocs).
- `tools/camera/bps-lsc.py` fait tout depuis l'EEPROM : en 4096 × 3072, blocs de 128 × 128,
  8 sous-blocs de 16, `x_delta` = 2^20 / 16 (hypothèse : pas de 2^20, à vérifier sur image).
- Pilote : section `lsc` du bloc de paramètres (version 2 ; la version 1 reste acceptée).
