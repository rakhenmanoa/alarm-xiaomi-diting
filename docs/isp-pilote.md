# Pilote de l'ISP matériel (BPS)

Retour à [l'état des lieux de l'ISP](isp-materiel.md) · [récapitulatif et reprise](isp-reprise.md) · [scoreboard](../scoreboard.md).

État au 23 nov. 2026 : le BPS traite une photo du HP1 en 1920 × 1080 par l'autotest du module
`qcom-camss-icp` (voir `isp-materiel.md`). Ce document décrit comment en faire un pilote
utilisable par libcamera et plasma-camera. La partie noyau (étape 2) fonctionne : première
image convertie par `/dev/video18` le 23 nov. 2026.

## Noyau : périphérique V4L2 mémoire à mémoire

- Un nœud `/dev/videoN` pour le BPS, sur le modèle des ISP hors ligne du noyau principal.
- File d'entrée (OUTPUT) : Bayer brut en RAW10 compacté (SGBRG10_CSI2P), tel que l'IFE
  l'écrit, sans conversion (format firmware `IMAGE_FORMAT_MIPI_10`, vérifié).
- File de sortie (CAPTURE) : NV12.
- Paramètres : `BpsIQSettings`, programme de registres (`REG_RANDOM`), tables gamma (DMI),
  bloc de découpage, réunis en un bloc. Le pilote les copie dans la mémoire partagée de l'ICP,
  construit `BpsFrameProcessData` et le `CDMProgramArray`, envoie `FRAME_PROCESS` et rend les
  tampons à la réponse du firmware. `CONFIG_IO` à la première image après chaque mise en route
  (le format ne change que flux arrêté).
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
2. ~~Pilote V4L2 mémoire à mémoire minimal, testé avec `v4l2-ctl`~~ : fait le 23 nov. 2026
   (`camss-bps.c`, dans le module `qcom-camss-icp`). La photo du HP1 (centre 1920 × 1080,
   RAW10 compacté) sort en NV12 identique à celle de l'autotest : couleurs, gamma, aucune
   cassure entre bandes, bas de l'image intact avec le plan UV collé au plan Y. Traitement
   d'environ 4 ms, session firmware ouverte et fermée par le flux.

3. Gestionnaire libcamera et IPA.

## Pilote écrit (`camss-bps.c`)

- Enregistré au chargement du module après l'initialisation HFI (`icp_m2m=0` pour s'en passer),
  nom `qcom-camss-bps`. API mono-plan.
- OUTPUT : `SRGGB10P`, `SGRBG10P`, `SBGGR10P`, `SGBRG10P` (ordres firmware 0 à 3, seul GB
  vérifié), largeur multiple de 16, `bytesperline` ≥ largeur × 5/4 (celui de l'IFE est gardé).
- CAPTURE : NV12 de même taille, plan UV juste après le plan Y, plage pleine BT.601.
- Les deux tampons ont 16 lignes de réserve à la fin (`sizeimage` les compte).
- Tampons `vb2_dma_contig` alloués pour le périphérique ICP : leurs adresses sont celles que le
  firmware voit (MMAP et DMABUF).
- Session firmware par descripteur ouvert : `CREATE_HANDLE` + `CONFIG_IO` à la première image,
  tampon CDM de la taille demandée par le firmware, fermeture à l'arrêt du flux.
- Le traitement d'une image tourne dans une file de travail ordonnée (les appels HFI dorment).

### Bloc de paramètres (version 1)

En-tête de 48 octets, petit-boutiste : `magic` « BPSP », `version` 1, `width`, `height` (taille
pour laquelle le découpage a été calculé ; avertissement si elle diffère), puis quatre sections
(`offset`, `length`) depuis le début du bloc : `BpsIQSettings` (≤ 1 276 octets), paires
(registre, valeur) en u32 (≤ 511), tables gamma (3 × 1 Ko exactement), découpage (≤ 16 Ko).
Bloc ≤ 32 Ko. Le pilote le prend :

- dans le contrôle `BPS Parameters` (tableau dynamique de u8, identifiant
  `V4L2_CID_USER_BASE + 0x10f0`), pour libcamera ;
- à défaut, à l'ouverture, dans `/lib/firmware/bps-params.bin` (paramètre `bps_params_file`),
  pour les essais avec `v4l2-ctl`.

`tools/camera/bps-params.py` assemble le bloc (`--iq`, `--regs`, `--cst`, `--wb G,B,R`,
`--gamma`, `--sl`).

### Essai

```
bps-params.py bps-params.bin --taille 1920x1080 \
    --iq 492,1,652,1,660,1,664,1,780,255,788,255,988,1 --cst --wb 1,1.415,1.513 \
    --regs $(bps-ccm.py 1 0 0 0 1 0 0 0 1) --gamma gamma.bin --sl sl-mipi-wb.bin
v4l2-ctl -d /dev/videoN --set-fmt-video-out=width=1920,height=1080,pixelformat=pGAA \
    --stream-out-mmap --stream-mmap --stream-from=hp1-pad.raw --stream-to=sortie.nv12 \
    --stream-count=1
```

Sur le téléphone, le module se compile hors de l'arbre avec
`make KDIR=~/linux-7.2.10 KBUILD_MODPOST_WARN=1` (pas de `Module.symvers` dans l'arbre) et
demande `modprobe -a v4l2-mem2mem videobuf2-dma-contig` avant `insmod`. Pour regarder la
sortie : `ffmpeg -f rawvideo -pix_fmt nv12 -s 1920x1080 -i sortie.nv12 -frames:v 1 sortie.png`
(le message sur un paquet trop court vient des 16 lignes de réserve, sans conséquence).

`hp1-pad.raw` : le centre compacté (`raw10-to-bps.py … --compacte`) suivi de 16 lignes nulles
(16 × 2 400 octets), pour faire `sizeimage`.
