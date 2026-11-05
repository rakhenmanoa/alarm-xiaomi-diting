# Pièges rencontrés — alarm-diting

Problèmes rencontrés pendant le portage et leurs contournements, par ordre d'apparition.
Retour au [README principal](../README.md).

## Pièges rencontrés

- **pacman 7 sous qemu-user ou sans Landlock** : « restricting filesystem access failed ». Ajoutez
  `DisableSandbox` dans `[options]` de `pacman.conf`. Le noyau `-5` active Landlock.
- **`diting-boot-successful` en échec** (« bad partition entry CRC ») : sur cette unité, la table
  GPT de secours de `sde` n'avait pas été mise à jour correctement par le bootloader. Il a fallu la
  reconstruire depuis la table principale. **Sans ce service, le slot devient inutilisable après
  7 démarrages.**
- **`linux-firmware` d'ALARM sans `linux-firmware-qcom`** : il faut l'installer, sinon
  `a730_sqe.fw` manque et le GPU ne démarre pas.
- **Audio** : le profil UCM d'origine ne passe pas avec PipeWire 1.6 (capture non routée → profil HiFi rejeté) et mélange gauche/droite sur les deux amplis ; corrigé par `pkgbuilds/diting-device/ucm-diting.py`.
- **Micros** : bruit blanc même ADC coupés, car les ports 1 et 2 du codec WCD938x (ADC1 et ADC4) partageaient le port maître SoundWire 1 et émettaient dans la même case. Le patch noyau `0903` envoie le port 2 du codec sur le port maître 2, et chaque port sur sa propre lane comme Android (`qcom,swr-tx-port-params` de l'overlay diting du `dtbo`, cas du bus à 9,6 MHz) : port 1 sur la lane 1, port 2 sur la lane 2, décalage 0. Une première version mettait les deux ports sur la lane 1 (décalages 0 et 1) : souffle à certains démarrages. L'ADC4 arrive alors sur `SWR_MIC5`, qu'utilise l'UCM, et la route DAPM suit (`TX SWR_INPUT5`), sinon l'ADC4 et MIC BIAS4 restent éteints. Test sans recompiler, sur `/boot/dtbs/qcom/sm8475-xiaomi-diting.dtb` : `fdtput -t u <dtb> /soc@0/soundwire@33b0000/codec@0,3 qcom,tx-port-mapping 1 2 3 4`, `fdtput -t bu <dtb> /soc@0/soundwire@33b0000 qcom,ports-lane-control 1 2 0 0`, `fdtput -t bu <dtb> /soc@0/soundwire@33b0000 qcom,ports-offset1 0 0 1 1`, `fdtput -t s <dtb> /sound audio-routing "AMIC1" "MIC BIAS1" "AMIC5" "MIC BIAS4" "TX SWR_INPUT0" "ADC1_OUTPUT" "TX SWR_INPUT5" "ADC4_OUTPUT"`, puis `mkinitcpio -P` et `diting-install-bootimg`.
- **Test du micro de Plasma** : plasma-settings plantait (`abort` dans `pa_stream_unref`, flux libéré deux fois par `microphonetest.cpp` de plasma-pa) ; évité par la latence minimale de `diting-device`. Le test reste peu fiable (niveau, hachures) : vérifier le micro avec l'enregistreur de Plasma Mobile (voir `audio/README.md`).
- **Modem** : ModemManager 1.24.2 plante toutes les 20 à 30 s (SIGSEGV dans `mm-netlink.c`), la 4G coupe ; recompiler avec le correctif officiel, voir `pkgbuilds/modemmanager-fix/`.
- **Données mobiles** : enregistré en 4G mais `cm error: no-service` à chaque connexion ; ModemManager laisse le client data sur l'abonnement `any` et le modem multi-SIM vise le mauvais. Le patch `0002` de `pkgbuilds/modemmanager-fix/` le lie à l'abonnement primaire. Ajouter `IgnorePkg = modemmanager libmm-glib` dans `/etc/pacman.conf`.
- **Wi-Fi** : l'économie d'énergie fait couper le Wi-Fi ; diting-device la désactive (`60-diting-wifi-powersave.conf` pour NetworkManager).
- **Fluidité** : le GPU et les petits cœurs montaient trop tard en fréquence (interface saccadée) ; `diting-perf.service` relève les minima (GPU 439 MHz, cœurs 1056 et 1113,6 MHz). À désactiver si la batterie compte plus que la fluidité.
- **Tactile, coins du bas** : le firmware Goodix retient les touchers des coins du bas (environ 10 % de large, 10 % de haut) jusqu'au relâchement ; `&123` et Entrée du clavier répondaient en retard. Le premier octet du module 0x64 de la configuration commande cette retenue (trouvé par essais avec `libinput debug-events`) : `diting-touch-cmd grip off` le coupe. Il n'est pas conservé à la mise sous tension du contrôleur : `diting-touch.service` et le hook de veille le réappliquent, puis envoient le réglage d'Android (`mode 2`, commande 0x18, trame du pilote Xiaomi `goodix_9916`). Sauvegarder d'abord la configuration avec `diting-touch-cmd config-read`. Bus SPI du tactile à 1 MHz (patch `0900`), qui fonctionne bien ; Android utilise 12 MHz.
- **Vibreur** : aucun pilote mainline pour le bloc haptique haute tension du PM8350B ; le patch noyau `0904` ajoute un pilote minimal (vibration constante, ff-memless, séquence du pilote `qcom-hv-haptics`, Vmax 2000 mV) et le nœud DT. Plasma Mobile vibre par feedbackd (`org.sigxcpu.Feedback.Haptic`) ; diting-device en dépend et installe la règle udev `90-diting-vibreur.rules` (`FEEDBACKD_TYPE=vibra`). Test : `diting-vibrate 300 100` ou `fbcli -e phone-incoming-call`. Pour tester sans recompiler le paquet : `extra/pm8350b-haptics/`.
- **PolicyKit** : sans règle, appels et USSD sont refusés à plasma-dialer ; diting-device installe `50-diting-modemmanager.rules` (groupe wheel).
- **libssc** demande `gobject-introspection`, et **libcamera** a besoin de `-Dwerror=false` avec
  GCC 16.
- Après un échec de `makepkg`, relancez avec **`-C`** : les fichiers créés par les patches restent
  sinon dans `src/`.
