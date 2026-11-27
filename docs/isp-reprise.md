# ISP matériel : récapitulatif et reprise

Retour à [l'état des lieux](isp-materiel.md) · [pilote V4L2](isp-pilote.md) · [scoreboard](../scoreboard.md).

Page écrite le 23 nov. 2026 pour reprendre le travail depuis une nouvelle conversation.
Le détail technique est dans `isp-materiel.md` (matériel, firmware, registres) et
`isp-pilote.md` (pilote, bloc de paramètres, commandes).

## Où on en est

L'ISP matériel du diting (Titan 680, SM8475) traite des images sous Linux.

- **ICP** : le processeur de contrôle démarre, son firmware `CICP.FW.5.0` (variante T480,
  API 0x23200ba) répond sur les files HFI. Module `qcom-camss-icp`, patch
  `pkgbuilds/linux-diting/0934-…ICP…patch` (fichiers `camss-icp.c`, `camss-icp-hfi.[ch]`,
  `camss-icp.h`, `camss-bps.c`), nœud DT dans le patch 0935.
- **BPS** : image complète traitée, RAW10 compacté (`SGBRG10P`) → NV12, en ~4 ms pour
  1920 × 1080. Chaîne en marche : dématriçage, balance des blancs (WB 2.0.1), matrice de
  couleurs (CC 1.3.1), gamma (GLUT, tables DMI, noir retiré dans la table), conversion YUV
  (CST 1.2).
- **Pilote V4L2** : le BPS est un nœud mémoire à mémoire (`/dev/video18` sur le téléphone,
  pilote `qcom-camss-bps`). Les réglages et le découpage arrivent en un bloc
  (`tools/camera/bps-params.py`), par le contrôle `bps_parameters` ou par
  `/lib/firmware/bps-params.bin`.
- **Découpage en bandes** : calculé par la bibliothèque de CamX `libipebpsstriping.so`
  exécutée dans Unicorn (`tools/camera/bps-striping.py`). Il dépend de la taille, du format
  d'entrée, de l'ordre Bayer et des modules activés.
- **Noir matériel (BLS 1.2.1)** : en marche depuis le 25 nov. 2026 (`tools/camera/bps-bls.py 54`,
  interrupteur 1060, découpage recalculé avec `1060,1`, gamma sans noir `glut-sans-noir.bin`).
- **Pas encore** : libcamera (qui traite toujours sur le CPU), réglages automatiques, IPE, JPEG.

## Ce qu'il faut avoir sous la main

Fichiers propriétaires, **jamais dans le dépôt** : à rejoindre à la nouvelle conversation
quand une tâche en a besoin (ils viennent de la sauvegarde HyperOS du téléphone).

| Fichier | Origine | Sert à |
|---|---|---|
| `libipebpsstriping.so` | `vendor/lib64` | découpage (`bps-striping.py`) |
| `camera.qcom.so` | `vendor/lib64/hw` | formats des registres (symboles dans `.gnu_debugdata`) |
| `com.qti.tuned.diting_semco_s5khp1_wide.bin` | `vendor/lib64/camera` | matrices de couleurs, réglages du HP1 |
| `CAMERA_ICP.elf` | firmware | DWARF des structures du firmware (`tools/camera/dwarf_struct.py`) |

Sur le téléphone, dans `/lib/firmware` : `hp1p.bin` (centre 1920 × 1080 compacté de la photo
de test), `sl-1920x1080-mipi.bin` (découpage), `glut-noir.bin` (gamma 2,2 avec noir à 67),
`bps-params.bin` (bloc par défaut du pilote).

## Commandes de base sur le téléphone

Dépôt dans `~/alarm-xiaomi-diting`, branche `isp-icp` (fusionnée dans `main`), noyau
7.2.10-diting, arbre source `~/linux-7.2.10`.

```
cd ~/alarm-xiaomi-diting/extra/camss-icp
git pull
make extraire && make KDIR=~/linux-7.2.10 KBUILD_MODPOST_WARN=1
sudo modprobe -a v4l2-mem2mem videobuf2-dma-contig
# après un démarrage à froid : réveiller la caméra AVANT le module, sinon « create handle: -110 »
cam -c 1 -s role=viewfinder -s role=raw,width=1920,height=1080 --capture=5 --file=/tmp/reveil-#.raw
sudo rmmod qcom_camss_icp; sudo insmod qcom-camss-icp.ko && sudo dmesg | tail -20
# jamais de rmmod après « create handle: -110 » (le téléphone se fige) : redémarrer
# le bloc caméra se met aussi en veille après une longue inactivité (~2 h) : même symptôme,
# même remède (redémarrage) ; une capture cam avant chaque essai l'évite.
# Le réveil ne dure pas : charger le module JUSTE après la capture cam (5 min plus tard,
# « -110 » le 27 nov. 2026). Sauver les fichiers de /tmp avant un redémarrage.
# Le numéro de caméra du HP1 change à chaque démarrage : cam -l | grep i2c-bus@1/camera@10
```

Conversion d'essai et visualisation :

```
python3 ../../tools/camera/bps-params.py /tmp/bps-params.bin --taille 1920x1080 \
    --iq 492,1,652,1,660,1,664,1,780,255,788,255,988,1,992,3,1060,1 --cst \
    --wb 1,1.80,2.04 --wb-noir 64 \
    --regs $(python3 ../../tools/camera/bps-ccm.py $(python3 ../../tools/camera/chromatix-cc13.py TUNED.bin --lux 300 --gains 1,1.80,2.04)) \
    --regs $(python3 ../../tools/camera/bps-bls.py 64) \
    --gamma /lib/firmware/glut-sans-noir.bin --sl /lib/firmware/sl-1920x1080-bls.bin
sudo cp /tmp/bps-params.bin /lib/firmware/
cat /lib/firmware/hp1p.bin <(head -c 38400 /dev/zero) > /tmp/hp1-pad.raw
v4l2-ctl -d /dev/video18 --set-fmt-video-out=width=1920,height=1080,pixelformat=pGAA \
    --stream-out-mmap --stream-mmap --stream-from=/tmp/hp1-pad.raw \
    --stream-to=/tmp/sortie.nv12 --stream-count=1
ffmpeg -loglevel error -f rawvideo -pix_fmt nv12 -s 1920x1080 -i /tmp/sortie.nv12 \
    -frames:v 1 -y /tmp/sortie.png
```

Noir du HP1 : **64** sur les quatre positions du motif, mesuré le 27 nov. 2026 sur une image
noire (objectif masqué) avec `raw10-phases.py … --noir 0`. Le 54 retenu à l'œil le 25 nov.
laissait les ombres magenta. Gains de la photo de test avec ce noir : `--wb 1,1.80,2.04`.

**Piège :** toujours donner la taille du flux brut du HP1 (`role=raw,width=1920,height=1080`, le
capteur passe alors en 4096 × 3072). Sans taille, le 27 nov. 2026, le bus I2C du capteur et du
moteur de mise au point a décroché (« Connection timed out ») et le téléphone s'est figé.
Le numéro de caméra du HP1 change d'un démarrage à l'autre : le trouver avec
`cam -l | grep i2c-bus@1/camera@10`.

Photo brute du capteur arrière (HP1) : `cam -c 1 -s role=viewfinder -s
role=raw,width=1920,height=1080 --capture=30 --file=hp1-#.raw` (4096 × 3072 SGBRG10_CSI2P ;
un flux brut seul donne une image noire). L'autotest du module (`icp_selftest=1 …`) reste
disponible, ses paramètres sont décrits dans `isp-materiel.md`.

## Ce qui reste à faire, dans l'ordre conseillé

### 1. Photo en pleine taille (4096 × 3072) — fait le 27 nov. 2026 en 3264 × 2448

**Première photo entière traitée par le BPS** (3264 × 2448, 8 MP, nette et en couleur, une passe
de 8 bandes). Correction du 27 nov. après-midi : cette photo venait du **S5K4H7 (grand-angle)**, pas
du HP1 (numéros de caméra changés au redémarrage ; 3264 × 2448 est un mode du S5K4H7, GBRG aussi).
Modes du HP1 vus par libcamera : 1920 × 1080 (≈ 120 i/s, exposition très courte), 4096 × 2304,
4096 × 3072, 8192 × 6144 ; découpages calculés
pour les deux (`sl-4096x3072.bin`, 10 bandes, et `sl-3264x2448.bin`, avec
`--iq 492,1,652,1,660,1,664,1,780,255,788,255,988,1,992,3,1060,1`), dans `/lib/firmware`.
Entrée : le fichier brut de `cam` plus 16 lignes nulles (16 × 4080 octets en 3264 de large).
Gains par moyenne grise avec `raw10-phases.py fichier 3264 2448`.

But : traiter la photo brute entière, sans recadrage.

1. Calculer le découpage (dans la conversation, avec `libipebpsstriping.so` jointe) :
   `bps-striping.py libipebpsstriping.so 4096 3072 -o sl-4096x3072.bin --in-format 2 --bayer 3
   --iq 492,1,652,1,660,1,664,1,780,255,788,255,988,1,992,3`. Vérifier que le fichier tient dans
   16 Ko (limite `BPS_SHM_SL_SIZE` du pilote ; sinon l'agrandir dans `camss-bps.c`, avec les
   régions de mémoire partagée qui suivent).
2. Bloc de paramètres avec `--taille 4096x3072` et ce découpage.
3. Entrée : la photo brute de `cam` telle quelle, pas de ligne 5 120 octets, plus 16 lignes
   nulles (16 × 5 120 octets). `v4l2-ctl … width=4096,height=3072,pixelformat=pGAA`.
4. Si la photo est trop grande pour une passe, la bibliothèque produit plusieurs passes : à
   vérifier dans sa sortie (`-v`).

### 2. Paramètres selon la taille, dans le pilote — fait le 27 nov. 2026

Vérifié : avec `/lib/firmware/bps-params-3264x2448.bin`, `v4l2-ctl` en 3264 × 2448 sans rien
d'autre donne la photo (branche `isp-libcamera`).


Le découpage n'est valable que pour une taille. Faire chercher au pilote, à la mise en route
du flux, `bps-params-LARGEURxHAUTEUR.bin` avant `bps-params.bin` (fonction
`bps_params_load_default` de `camss-bps.c`, à appeler aussi au changement de format). Ainsi
libcamera peut s'en servir sans IPA, avec un fichier par taille installé dans `/lib/firmware`.

### 3. Premier branchement sur libcamera (convertisseur du gestionnaire `simple`) — écrit le 27 nov. 2026, à tester

**27 nov. 2026 : premières images par libcamera.** `LIBCAMERA_QCOM_BPS=1 cam -c N -s
role=viewfinder,pixelformat=NV12 --capture=3 --file=/tmp/bps-#.nv12` donne des images NV12
1920 × 1080 traitées par le BPS (capteur HP1 en 1920 × 1080, tampons CSI importés). Elles sont
très sombres : sans ISP logiciel, pas d'exposition automatique ; réglages du BPS figés
(`bps-params.bin` 1920 × 1080). Corrections qu'il a fallu : `queue_setup` doit donner pour
l'entrée la taille exacte de l'image (vb2 s'en sert comme minimum des tampons importés), et le
pilote répond aux requêtes de sélection (recadrage = image entière). **HP1 en 4096 × 3072 par le BPS, à 30 i/s, en continu** (27 nov. 2026) :
`LIBCAMERA_QCOM_BPS=1 cam -c N -s role=viewfinder,pixelformat=NV12,width=4096,height=3072`.
Il a fallu relever le masque DMA du module après l'allocation de la mémoire HFI : les tampons
d'image vont dans la région « io 1 » du firmware (0x10c00000-0xe0000000) ; sous 256 Mio, les
tampons NV12 de 19 Mo ne se mappaient plus (« failed to map dmabuf »). Dans ce mode, blanking et
gain changés **pendant le flux** sont appliqués (cadence 30 → 23,5 i/s, luminosité moyenne
62 → 88 avec le gain 4× → 64×) : l'IPA pourra régler l'exposition à chaque image.
Piège : après un `rmmod` du module en marche, le premier `insmod` échoue souvent (« probe …
failed with error -110 », module chargé sans périphérique) ; `rmmod` puis capture `cam` puis
`insmod` à nouveau marche.
Le mode 1920 × 1080 du HP1 tourne à ≈ 120 i/s : images sombres (exposition ≤ 1/120 s), et
le blanking comme le gain changés pendant le flux restent sans effet dans ce mode (la cadence ne
bouge pas). Préférer 4096 × 3072 (30 i/s) pour le BPS.
Exposition à la main : régler le capteur **avant** de lancer `cam`
(`sd=$(media-ctl -d /dev/media0 -e "s5khp1 3-0010")`, puis `v4l2-ctl -d $sd --set-ctrl
exposure=2584,analogue_gain=2048` : image lisible dans une pièce sombre). libcamera remet le
blanking vertical au minimum à la configuration (exposition plafonnée à 2584 lignes en
1920 × 1080), et les changements faits pendant le flux sont affichés par le capteur mais sans
effet sur l'image : à vérifier dans le pilote s5khp1 (écriture des contrôles en cours de flux)
avant l'IPA. Suite : exposition automatique (IPA + pilote du capteur), `bps-params-1920x1080.bin` avec un découpage refait avec
`992,3`, essai dans plasma-camera.

Fait sur la branche `isp-libcamera` : dans `camss-bps.c` (patch 0934), un périphérique média
`qcom-camss-bps` et le chargement de `bps-params-LxH.bin` à chaque changement de format ;
patch libcamera 0017 (`libcamera-diting` pkgrel 13), où le BPS ne remplace l'ISP logiciel que si
`LIBCAMERA_QCOM_BPS=1` est dans l'environnement (les autres caméras et l'usage normal ne changent
pas). Prérequis : le découpage en 4096 × 3072 (étape 1), taille du flux brut du HP1.


Le gestionnaire `simple` de libcamera sait déjà envoyer les images capturées à un
convertisseur mémoire à mémoire (`V4L2M2MConverter`). C'est le chemin le plus court vers
une caméra utilisable par les applications.

1. Noyau : donner au BPS un périphérique média (`media_device_init` + `media_device_register`,
   `v4l2_m2m_register_media_controller`, `driver_name` = `qcom-camss-bps`), comme
   `drivers/media/test-drivers/vim2m.c`. libcamera trouve les convertisseurs par ce nom.
2. libcamera :
   - `src/libcamera/converter/converter_v4l2_m2m.cpp` : ajouter `"qcom-camss-bps"` à
     `compatibles` ;
   - `src/libcamera/pipeline/simple/simple.cpp` : remplacer `{ "qcom-camss", {}, true }` par
     `{ "qcom-camss", { { "qcom-camss-bps", 1 } }, false }` (convertisseur et ISP logiciel
     s'excluent).
   - Paquet libcamera du dépôt à reconstruire avec ce patch.
3. Essai : `cam -c 1 -s role=viewfinder,pixelformat=NV12 --capture=3 --file=…`, puis
   plasma-camera.
4. Limites attendues : réglages fixes (balance des blancs et exposition figées), la taille de
   sortie du convertisseur doit être celle de l'entrée.

### 4. ~~Soustraction du noir en matériel (BLS 1.2.1)~~ : fait le 25 nov. 2026

Format décodé dans `camera.qcom.so` et vérifié sur le téléphone (voir `isp-materiel.md`) :
décalage à l'échelle 17 bits (noir 54 sur 10 bits), gain Q11, seuils de noir par canal à laisser à
`0x4000`. Outil : `tools/camera/bps-bls.py NOIR10`. Le découpage se calcule avec `1060,1` dans
`--iq` (fichier `/lib/firmware/sl-1920x1080-bls.bin`), le gamma sans noir avec
`bps-gamma.py … 2.2 1.0 0`. Reste à faire : un noir par canal (le rouge a un noir plus bas que le
vert et le bleu), en refaisant la mesure du noir par canal sur des photos du capteur couvert.

### 5. Matrice de couleurs selon l'éclairage (en cours depuis le 27 nov. 2026)

**Premier vrai rendu couleur le 27 nov. 2026** (mur neutre 118/117/118, ombres neutres, tissu beige
orangé, fleur rouge) avec : récupération de canal coupée (`--iq 992,3`), gains calculés sur l'image
brute sans le noir (`raw10-phases.py` : G/B, G/R du mur → `--wb 1,1.71,1.91`), noir gardé par la
balance des blancs (`--wb-noir 54`, le BLS est placé après elle), seuils du BLS à 0 et matrice du
fichier de réglage à 4000 K. `TUNED.bin` = `com.qti.tuned.diting_semco_s5khp1_wide.bin` de la
sauvegarde.

La matrice identité donne des couleurs proches ; celle prise au hasard (`1.74 -0.5794 …`,
d'une variante IPE) sature et tire l'orange vers le rose. CamX interpole entre plusieurs
matrices selon l'indice de luminosité et la température de couleur.

1. ~~Décoder l'arbre du module CC 1.3~~ : fait, `tools/camera/chromatix-cc13.py`
   (format dans l'en-tête du script). Le fichier a 84 instances du module, en 5 groupes ;
   l'instance BPS 929 a les mêmes matrices que l'IPE par défaut (instance 4) : c'est elle
   par défaut. 19 régions : 5 plages d'indice de luminosité (la plus sombre, 600 et plus,
   donne la matrice identité) × 5 plages de température de couleur (2500 à 10000 K).
2. Essai sur la photo de test : `--regs $(bps-ccm.py $(chromatix-cc13.py TUNED.bin --lux L
   --cct K))` à la place de la matrice identité, pour quelques températures de couleur.
3. Température de couleur estimée à partir des gains (27 nov. 2026) : `chromatix-cc13.py TUNED.bin
   --lux 300 --gains 1,1.71,1.91` (points de référence `refPtV1` du fichier, projection sur le
   lieu D75-D65-D50-A-Horizon ; 5600 K environ pour la photo de test). Reste l'indice de
   luminosité à partir de l'exposition, dans l'IPA (étape 6).

### 6. IPA libcamera (réglages automatiques)

Gestionnaire `diting` dérivé de `simple`, ou IPA branché sur le convertisseur : balance des
blancs et exposition d'abord avec les statistiques du SoftISP, puis celles du BPS (sorties
`STATS_BG`, `STATS_BHIST`, indices 5 et 6 des images du BPS, à décrire dans `CONFIG_IO`).
L'IPA construit le bloc de paramètres à chaque image et le passe par le contrôle
`bps_parameters`.

### 7. Découpage précalculé à l'installation

La bibliothèque de CamX n'est pas redistribuable : script qui, à partir de la sauvegarde
HyperOS de l'utilisateur, calcule les découpages des tailles utiles (`bps-striping.py`, sur le
téléphone avec `pip install unicorn pyelftools`) et les installe dans `/lib/firmware`.

### 8. IPE puis JPEG

IPE (débruitage, netteté, mise à l'échelle) : même méthode que le BPS (session déjà créée par
l'autotest, `CONFIG_IO` accepté) ; structures `IpeFrameProcessData` dans le DWARF du firmware,
découpage par la bibliothèque `libipebpsstriping` (fonctions IPE). JPEG matériel ensuite.

### Entretien

- `Module.symvers` manque dans `~/linux-7.2.10` : d'où `KBUILD_MODPOST_WARN=1`. Le recopier
  depuis le paquet du noyau ou refaire `make modules_prepare` complet évite l'option.
- Le patch 0934 entre dans le paquet `linux-diting` : à la prochaine construction du noyau,
  vérifier que `CONFIG_VIDEO_QCOM_CAMSS_HARD_ISP=m` y est (il sélectionne maintenant
  `V4L2_MEM2MEM_DEV` et `VIDEOBUF2_DMA_CONTIG`).
- Modifier le pilote : extraire les fichiers du patch
  (`make extraire` dans `extra/camss-icp`), les modifier, puis reconstruire les sections du
  patch à partir des fichiers (les sections « new file » du patch sont le texte des fichiers
  préfixé de `+`, avec le nombre de lignes dans l'en-tête `@@ -0,0 +1,N @@`).

## Message pour reprendre

À coller au début d'une nouvelle conversation :

> Pilote ISP matériel du Xiaomi 12T Pro « diting », dépôt rakhenmanoa/alarm-xiaomi-diting,
> branche isp-icp. Lis d'abord docs/isp-reprise.md, puis isp-pilote.md et isp-materiel.md.
> Le BPS marche par V4L2 (/dev/video18). On attaque l'étape N de la liste « Ce qui reste à
> faire ». Réponds en français, commandes une par une ; je te colle les sorties du téléphone.
