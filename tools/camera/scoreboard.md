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
| Ombrage de l'objectif (LSC 4.1.1) | ⬜ | idem |
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

## Outils (`tools/camera`)

| Outil | Rôle |
|---|---|
| `bps-params.py` | bloc de paramètres du pilote (IQ, registres, gamma, découpage) |
| `bps-striping.py` | découpage en bandes avec `libipebpsstriping.so` (Unicorn) |
| `bps-bls.py`, `bps-ccm.py`, `bps-gamma.py` | registres du noir, de la matrice, tables gamma |
| `chromatix-cc13.py` | matrices de couleurs du fichier de réglage, température de couleur depuis les gains |
| `raw10-phases.py` | moyennes des 4 positions Bayer d'une image RAW10 (noir, ordre, gains) |
| `raw10-to-bps.py` | recadrage et compactage d'une image brute pour le BPS |
| `dwarf_*.py`, `xtensa-dis.py`, `cdm-decode.py` | lecture du firmware de l'ICP et des programmes CDM |
| `camx-infos.py`, `chromatix_regs.py`, `dt-*.py` | fichiers de CamX, réglages, device tree Android |

Fichiers propriétaires, jamais dans le dépôt (sauvegarde HyperOS) : `libipebpsstriping.so`,
`camera.qcom.so`, `com.qti.tuned.diting_semco_s5khp1_wide.bin`, `CAMERA_ICP.elf`.
