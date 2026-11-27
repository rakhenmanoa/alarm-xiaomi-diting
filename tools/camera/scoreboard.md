# Caméra et ISP matériel — état détaillé

Suivi de la caméra du Xiaomi 12T Pro (`diting`) sous Linux : capteurs, ISP logiciel de libcamera,
ISP matériel Qualcomm (Titan 680 : ICP, BPS, IPE). Mis à jour le 27 nov. 2026.
Retour au [scoreboard général](../../scoreboard.md) · détails techniques :
[isp-materiel.md](../../docs/isp-materiel.md), [isp-pilote.md](../../docs/isp-pilote.md),
reprise du travail : [isp-reprise.md](../../docs/isp-reprise.md).

✅ fonctionne · 🟡 partiel ou à améliorer · ❌ ne fonctionne pas · ⬜ pas encore commencé

## Capteurs

| Élément | État | Détails |
|---|:---:|---|
| Principale HP1 (S5KHP1, 200 Mpx) | ✅ | modes 1920 × 1080 (≈ 120 i/s), 4096 × 2304, 4096 × 3072 (30 i/s), 8192 × 6144 ; GBRG, noir à 64 (mesuré sur image noire) |
| Exposition et gain en cours de flux | 🟡 | appliqués en 4096 × 3072 ; sans effet en 1920 × 1080 (mode 120 i/s) |
| Autofocus à détection de phase (PDAF) | ⬜ | le HP1 a des pixels de phase ; sous Android, les données de phase du capteur et la calibration (mémoire OTP du module) vont à la bibliothèque PDAF propriétaire de Qualcomm. À explorer : flux des données de phase (canal virtuel CSI ?), calibration, puis algorithme libre |
| Stabilisation optique (OIS) | ⬜ | contrôleur OIS séparé sur le bus du module (puce et firmware à identifier dans le device tree Android) ; aucun pilote sous Linux : objectif ni stabilisé ni piloté |
| Moteur de mise au point (AK7375) | ✅ | `focus_absolute` 0-1023, plus haut = plus proche ; ≈ 150 pour une pièce, plus bas pour le lointain |
| Ultra grand-angle (S5K4H7) | ✅ | 3264 × 2448 et 3264 × 1836, GBRG ; traité aussi par le BPS (photo 8 Mpx) |
| Frontale (IMX596), macro (GC02M1) | ✅ | ISP logiciel, 30 i/s |
| Numéro de caméra dans libcamera | ⚠️ | change à chaque démarrage : `cam -l \| grep i2c-bus@1/camera@10` pour le HP1 |

## ISP logiciel (libcamera, chemin par défaut)

| Fonction | État | Détails |
|---|:---:|---|
| Aperçu et photo dans plasma-camera | ✅ | aperçu 12 Mpx, photo 50 Mpx (patches 0005-0016) |
| Exposition, balance des blancs | ✅ | IPA `simple`, réglages de contraste et saturation |
| Autofocus par contraste | 🟡 | recherche locale, choix du sujet au toucher ; réglette manuelle de secours |
| Dématriçage et netteté | ✅ | sur le GPU (Malvar-He-Cutler) |

## ISP matériel : firmware et pilote noyau

| Élément | État | Détails |
|---|:---:|---|
| ICP (processeur de contrôle) | ✅ | firmware `CAMERA_ICP` de la sauvegarde HyperOS, HFI prêt ; module `qcom-camss-icp` (patch 0934, nœud DT 0935) |
| Session BPS (création, CONFIG_IO, FRAME_PROCESS) | ✅ | structures lues dans le DWARF du firmware |
| Pilote V4L2 mémoire à mémoire | ✅ | `/dev/video18`, RAW10 compacté → NV12 ; périphérique média `qcom-camss-bps` pour libcamera |
| Paramètres par taille | ✅ | `/lib/firmware/bps-params-LxH.bin` chargé au changement de format, sinon `bps-params.bin` ; contrôle `bps_parameters` pour les envoyer à chaque image |
| Tampons d'image | ✅ | région « io 1 » du firmware (0x10c00000-0xe0000000) ; tampons CSI importés à leur taille exacte |
| Découpage en bandes | 🟡 | `libipebpsstriping.so` de CamX exécutée dans Unicorn (`bps-striping.py`) ; calculé pour 1920 × 1080, 3264 × 2448, 4096 × 3072 ; pas encore pour 8192 × 6144 ni à l'installation |
| Démarrage du module | 🟡 | réveiller la caméra (`cam`) juste avant `insmod` ; après un `rmmod`, le premier `insmod` échoue souvent (refaire) ; jamais de `rmmod` après « create handle: -110 » |
| Blocage du téléphone | ⚠️ | un blocage inexpliqué le 27 nov. (capture `cam` + nouveau module), non reproduit |

## ISP matériel : chaîne d'image du BPS

| Module | État | Détails |
|---|:---:|---|
| Soustraction du noir (BLS 1.2.1) | ✅ | noir 64, seuils à 0 ; agit après la balance des blancs |
| Balance des blancs (WB 2.0.1) | ✅ | gains G, B, R ; noir préservé (`--wb-noir`) ; récupération de canal **coupée** (`--iq 992,3`, sinon le vert copie le bleu) |
| Dématriçage (Demosaic 3.6.1) | ✅ | valeurs par défaut ; registres et interrupteurs décodés (`0x4968`, octets 492-512) |
| Matrice de couleurs (CC 1.3.1) | ✅ | matrices du fichier de réglage interpolées selon la température de couleur (`chromatix-cc13.py`) |
| Gamma (GLUT 1.5.1) | 🟡 | gamma 2,2 fixe, léger voile dans les noirs à vérifier |
| Conversion YUV (CST 1.2) | ✅ | BT.601 plage pleine, NV12 |
| Réduction du bruit (ABF 4.1) | ⬜ | valeurs dans le fichier de réglage, format des registres à décoder |
| Pixels défectueux / PDAF (BPC-PDPC 3.1.1) | ⬜ | idem |
| Ombrage de l'objectif (LSC 4.1.1) | 🟡 | tables (DMI 0x3608, programme 5), 13 registres à 0x3668 et octets 444-488 de `BpsIQSettings` relevés ; `bps-lsc.py` depuis l'EEPROM, section `lsc` du bloc (version 2) ; découpage avec LSC calculé ; à tester sur image |
| Mappage de tons (GTM, LTM) | ⬜ | |
| Sorties réduites DS4 / DS16 | ⬜ | pour un aperçu léger depuis le 12 Mpx, comme sous Android |
| Statistiques matérielles (BG, BHIST) | ⬜ | pour remplacer la mesure sur l'image sortie |
| IPE (débruitage, netteté, mise à l'échelle) | ⬜ | session déjà créée par l'autotest, structures à décrire |
| JPEG matériel | ⬜ | |

## ISP matériel dans libcamera (`LIBCAMERA_QCOM_BPS=1`)

| Fonction | État | Détails |
|---|:---:|---|
| BPS comme convertisseur du gestionnaire `simple` | ✅ | patch 0017 ; sans la variable, rien ne change (ISP logiciel) |
| HP1 en 12 Mpx à 30 i/s | ✅ | `cam … role=viewfinder,pixelformat=NV12,width=4096,height=3072` |
| Exposition automatique | 🟡 | patch 0018 : mesure de l'image sortie, exposition puis gain ; plafonnée à 1/30 s (blanking non réglé) |
| Balance des blancs automatique | 🟡 | patch 0019 : moyenne grise, gains réécrits dans le bloc et renvoyés au pilote ; converge en ~6 mesures |
| Matrice selon la température de couleur | ⬜ | calcul prêt (`chromatix-cc13.py --gains`), à porter dans libcamera |
| Autofocus | ❌ | pas sur ce chemin ; régler `focus_absolute` à la main avant |
| plasma-camera | 🟡 | aperçu ✅ (1920 × 1080, mode 120 i/s du capteur : sombre et bruité en intérieur) ; photo = image de l'aperçu (2 Mpx) |
| Photo pleine résolution | ⬜ | 12 Mpx prêt côté BPS ; 50 Mpx demande découpage et bloc |

## Fonctions du matériel (fiche technique), à couvrir

Relevé des caractéristiques annoncées du module photo et du capteur (Xiaomi, Samsung), pour ne rien
oublier. Sources en bas de page.

| Fonction | État | Ce qu'il faudrait sous Linux |
|---|:---:|---|
| Regroupement de pixels HP1 (ChameleonCell : 16 en 1 → 12,5 Mpx, 4 en 1 → 50 Mpx, plein 200 Mpx) | 🟡 | modes 12,5 et 50 Mpx exposés par le pilote ; 200 Mpx (16384 × 12288) : patch `0908` côté CSI, à vérifier (remosaïquage dans le capteur ?) |
| Zoom dans le capteur (recadrage 2×/4× sans perte, « in-sensor zoom ») | ⬜ | modes recadrés du capteur à décrire dans le pilote `s5khp1` |
| HDR décalé (Staggered HDR, 3 expositions, 100 dB) | ⬜ | trames multiples par canaux virtuels CSI, fusion (BPS/IFE HDR ou logiciel) |
| Smart-ISO / Smart-ISO Pro (double gain de conversion) | ⬜ | registres du capteur ; Smart-ISO Pro = HDR vidéo par fusion des deux gains |
| Multi-échantillonnage (lectures multiples moyennées) | ⬜ | registres du capteur, basse lumière |
| Double Super PD (pixels de phase sous micro-lentilles) | ⬜ | voir PDAF plus haut ; correction de ces pixels par le module PDPC du BPS |
| Calibration du module (EEPROM P24C128F 16 Ko, `3-0051`, pilote `at24` déjà actif) | 🟡 | lue et décodée par `hp1-eeprom.py` (sommes de contrôle bonnes) : ombrage LSC 17 × 13 × 4 canaux, balance des blancs d'usine (module et référence), cartes de gain PDAF gauche/droite 17 × 13, blocs PDAF « PD01 » et « PDXT » ; LSC branché sur le BPS (à tester) ; reste à identifier les blocs 01, 02, 05, 06 |
| Composant inconnu à 0x59 sur le bus du HP1 (répond, sans pilote) | ⬜ | peut-être le contrôleur de l'OIS ; à identifier |
| Capteur de spectre de couleur / anti-scintillement | ⬜ | capteur d'ambiance annoncé (« colour spectrum sensor ») : aide pour la balance des blancs et le scintillement des éclairages ; à identifier |
| Flash double LED bicolore | 🟡 | une seule voie pilotée (patch `0912`) ; la seconde teinte et le flash synchronisé à la prise de vue manquent |
| Ultra grand-angle, mise au point fixe (infini) ; macro, mise au point fixe (~4 cm) | ✅ | rien à piloter |
| Stabilisation électronique (EIS, gyroscope) | ⬜ | gyroscope disponible côté capteurs ; stabilisation logicielle ou par l'IPE |
| Vidéo 8K 24 i/s, 4K 30/60 i/s, 1080p jusqu'à 240 i/s, HDR10+ | ⬜ | modes capteur ; encodeur matériel (pilote `qcom_iris` chargé, à essayer) ; débit mémoire/horloges du BPS-IPE |
| Ralenti jusqu'à 1920 i/s (résolution réduite) | ⬜ | sans doute interpolé ou mode capteur dédié, faible priorité |
| Photo RAW (DNG), formats HEIF/JPEG | 🟡 | brut disponible (`cam` rôle raw) ; JPEG matériel non commencé |

Sources : [Xiaomi 12T Pro, fiche technique](https://www.mi.com/global/xiaomi-12t-pro/specs/),
[GSMArena, test du 12T Pro](https://m.gsmarena.com/xiaomi_12t_pro-review-2484.php),
[Samsung, ISOCELL HP1](https://news.samsung.com/global/isocell-hp1-resolution-redefined),
[GSMArena, fonctions du HP1](https://m.gsmarena.com/samsung_details_the_cutting_edge_features_of_its_200mp_isocell_hp1_sensor-news-51047.php).

## Outils (`tools/camera`)

| Outil | Rôle |
|---|---|
| `bps-params.py` | bloc de paramètres du pilote (IQ, registres, gamma, découpage) |
| `bps-striping.py` | découpage en bandes avec `libipebpsstriping.so` (Unicorn) |
| `bps-bls.py`, `bps-ccm.py`, `bps-gamma.py` | registres du noir, de la matrice, tables gamma |
| `chromatix-cc13.py` | matrices de couleurs du fichier de réglage, température de couleur depuis les gains |
| `raw10-phases.py` | moyennes des 4 positions Bayer d'une image RAW10 (noir, ordre, gains) |
| `raw10-to-bps.py` | recadrage et compactage d'une image brute pour le BPS |
| `hp1-eeprom.py` | décode l'EEPROM de calibration du HP1 (ombrage, balance des blancs, PDAF) |
| `bps-lsc.py` | tables, registres et réglages du module d'ombrage (LSC) depuis l'EEPROM |
| `dwarf_*.py`, `xtensa-dis.py`, `cdm-decode.py` | lecture du firmware de l'ICP et des programmes CDM |
| `camx-infos.py`, `chromatix_regs.py`, `dt-*.py` | fichiers de CamX, réglages, device tree Android |

Fichiers propriétaires, jamais dans le dépôt (sauvegarde HyperOS) : `libipebpsstriping.so`,
`camera.qcom.so`, `com.qti.tuned.diting_semco_s5khp1_wide.bin`, `CAMERA_ICP.elf`.
