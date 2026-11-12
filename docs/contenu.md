# Ce que contient l'image — alarm-diting

Composants embarqués dans l'image ALARM + Plasma Mobile du Xiaomi 12T Pro (`diting`).
Retour au [README principal](../README.md).

## Composants

| Domaine | Composants |
|---|---|
| Noyau | `linux-diting` 7.2.10 + patches diting-ubuntu, Image non compressée, DTB avec ids ABL |
| Démarrage | mkinitcpio minimal ; hook `/usr/lib/initcpio/post/diting-bootimg` → `/boot/diting/{boot,vendor_boot}.img` |
| Firmware | `firmware-xiaomi-diting` (le vôtre) + `linux-firmware-qcom`/`-atheros` |
| GPU | Mesa freedreno / Turnip |
| Audio, appels | topologie AudioReach, UCM, protection des haut-parleurs, `diting-voice-call`, `diting-callaudio` (remplace le service de callaudiod pour plasma-dialer), `fastrpc-diting` |
| Modem | rmtfs (lecture seule), tqftpserv, qrtr, `diting-modem-setup`, ModemManager |
| Capteurs | `hexagonrpc-diting`, `libssc`, `iio-sensor-proxy-diting` (rotation, proximité, lumière, boussole) ; gyroscope lisible avec `ssccli --sensor gyroscope` ; `diting-proximite-appel` éteint l'écran contre l'oreille pendant un appel (Plasma Mobile ne le fait pas) ; `diting-lever-reveil` rallume l'écran quand on lève le téléphone (tilt to wake du SLPI, patch libssc 0001 ; réglé plus réactif dans `firmware-xiaomi-diting`, capteur ouvert seulement écran éteint, ignoré si la proximité est couverte ou pendant un appel) ; luminosité automatique de Plasma 6.6+ (KWin, via `qt6-sensors` et le capteur de lumière) à cocher dans les paramètres d'affichage (`kcmshell6 kcm_kscreen`) |
| Vidéo | `libva-v4l2-stateful-diting` (H.264/HEVC/VP9 matériels, VA-API) |
| Caméra | `libcamera-diting` 0.7.0 + patches (si ALARM est en 0.7.x), dont le patch 0006 : trames allongées jusqu'à 100 ms en basse lumière, AGC proportionnel, autofocus avec arrêt après le pic (réglages `maxFrameDuration`, `gainThreshold`, `rangeMin`/`rangeMax`… dans les fichiers de réglage) ; patch 0016 : autofocus par recherche autour de la position courante (plus de balayage complet à chaque relance), attente d'une scène stable, netteté vérifiée, sujet le plus proche au premier balayage, zone de mise au point au choix (`AfWindows`, toucher l'aperçu dans `plasma-camera-diting`, appui long pour revenir au centre ; réglages `settleFrames`, `climbStep`, `climbMargin`, `changeThreshold`… dans `s5khp1.yaml`) ; caméra principale jusqu'à 64x de gain analogique (patch noyau 0907) ; frontale IMX596 et macro GC02M1 : pilotes `imx596`/`gc02m1` (patches noyau 0905-0906), tables `imx596-front.camregs`/`gc02m1-macro.camregs` générées par `tools/camera/chromatix-to-camregs.py` ; testées dans plasma-camera (30 im/s, couleurs et orientation correctes, frontale jusqu'à 2592x1952) |
| Bureau | Plasma Mobile (SDDM Wayland, connexion automatique), plasma-dialer, Spacebar, Angelfish… |
