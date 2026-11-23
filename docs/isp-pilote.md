# Pilote de l'ISP matériel (BPS) : architecture prévue

État au 23 nov. 2026 : le BPS traite une photo du HP1 en 1920 × 1080 par l'autotest du module
`qcom-camss-icp` (voir `isp-materiel.md`). Ce document décrit comment en faire un pilote
utilisable par libcamera et plasma-camera. Rien de ceci n'est encore écrit.

## Noyau : périphérique V4L2 mémoire à mémoire

- Un nœud `/dev/videoN` pour le BPS, sur le modèle des ISP hors ligne du noyau principal.
- File d'entrée (OUTPUT) : Bayer brut en RAW10 compacté (SGBRG10_CSI2P), tel que l'IFE
  l'écrit, sans conversion (format firmware `IMAGE_FORMAT_MIPI_10`, vérifié).
- File de sortie (CAPTURE) : NV12.
- File de paramètres (tampon méta par image) : `BpsIQSettings`, programme de registres
  (`REG_RANDOM`), tables gamma (DMI), bloc de découpage. Le pilote les copie dans la mémoire
  partagée de l'ICP, construit `BpsFrameProcessData` et le `CDMProgramArray`, envoie
  `FRAME_PROCESS` et rend les tampons à la réponse du firmware. `CONFIG_IO` à chaque changement
  de format.
- Le pilote ne calcule rien : comme le pilote Android, il transmet.

## libcamera : gestionnaire `diting`

- Dérivé du gestionnaire `simple` : capture du brut par l'IFE (RDI), envoi au BPS à la place du
  SoftISP, NV12 vers l'application.
- Module d'algorithmes (IPA) : balance des blancs, exposition, choix de la matrice de couleurs,
  noir, gamma ; d'abord avec les statistiques du SoftISP, plus tard celles du BPS (BG, BHIST).
- Programmes de registres construits avec les formats relevés dans CamX (CST, WB, CC, GLUT).

## Découpage en bandes

La bibliothèque de CamX (`libipebpsstriping.so`) est un binaire Android non redistribuable. Le
découpage ne dépend que de la taille, du format d'entrée et des modules activés : il sera
précalculé par `tools/camera/bps-striping.py` (émulateur Unicorn) pour les combinaisons utiles,
à l'installation, à partir de la sauvegarde HyperOS de l'utilisateur, et lu par libcamera.

## Étapes

1. ~~Vérifier l'entrée RAW10 compactée~~ : fait le 23 nov. 2026. Avec `icp_selftest_bps_in=2`,
   le centre de la photo brute en RAW10 compacté (`raw10-to-bps.py … --compacte`, pas de ligne
   2 400 octets) et le découpage calculé pour ce format (`bps-striping.py … --in-format 2`),
   le BPS rend la même image qu'avec l'entrée 16 bits. Le pilote pourra donc prendre les tampons
   de l'IFE tels quels.
2. Pilote V4L2 mémoire à mémoire minimal (une image, paramètres fixes), testé avec
   `v4l2-ctl`.
3. Gestionnaire libcamera et IPA.
