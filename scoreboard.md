# État du matériel — alarm-diting

Suivi du matériel du Xiaomi 12T Pro (`diting`) sous Arch Linux ARM + Plasma Mobile.
Retour au [README principal](README.md).

## État du matériel

✅ fonctionne · 🟡 partiel ou à améliorer · ❌ ne fonctionne pas · ⬜ pas encore testé

Testé sur le téléphone sous ALARM, sauf mention « diting-ubuntu » : ces lignes reprennent le
[suivi de diting-ubuntu](https://github.com/royka1/diting-ubuntu/blob/main/docs/BRINGUP_STATUS.md),
même noyau, pas encore revérifiées ici.

| Matériel | État | Détails / reste à faire |
|---|:---:|---|
| **Système** | | |
| Démarrage, slots A/B | ✅ | `diting-boot-successful` obligatoire, sinon le slot tombe après 7 démarrages |
| Redémarrage vers fastboot | ✅ | `diting-reboot-bootloader` (diting-ubuntu) |
| Veille | 🟡 | s2idle et deep fonctionnent, mais pas d'effondrement complet du SoC (CX/AOSD/DDR) : consommation en veille à réduire |
| Horloge (RTC du PMIC) | ⬜ | `rtc-pm8xxx` en lecture seule (comme sous Android) ; `diting-rtc` garde le décalage heure réelle − compteur dans `/var/lib/diting-rtc` et remet l'heure à l'allumage sans réseau. Réveils par alarme RTC déjà vérifiés (diting-ubuntu). Voir [rtc](rtc/README.md) |
| Gestion thermique | 🟡 | zones thermiques du PMIC (alerte à 95 °C) ; thermistances de la carte (patch `0914` : peau, flash, poche, connecteur USB, chargeur, PA, Wi-Fi) lues correctement (33 à 37 °C au repos) ; pas de régulation sur ces zones ni de limitation de courant BCL (pas de pilote) |
| **Affichage** | | |
| Écran (dalles 36 02 0b et 42 02 0a) | ✅ | 60, 90 et 120 Hz, DSC |
| Luminosité | ✅ | courbe réglable (patch `0916`), validée avec les valeurs par défaut (`bl_min_dbv=60`, `bl_courbe=70`, modifiables à chaud dans `/sys/module/panel_xiaomi_l12_36_02_0b/parameters/`) |
| HBM (plein soleil) | 🟡 | patch `0917` (option `bl_hbm`, désactivée par défaut) : aucune différence visible en écrivant DBV 4095 sur 2 octets. Piste : la séquence Xiaomi écrit `0x51` sur 4 octets (`00 00 0f fe`, second champ = plafond HBM ?) et utilise `0x87`/`0x88`. Impression aussi que 100 % est moins lumineux qu'avant `0916`, non vérifiée (comparer avec le module d'origine) |
| Écran toujours allumé (AOD) | ⬜ | Xiaomi utilise DBV 20 / 252 en veille ; demande un mode basse consommation de la dalle et sa prise en charge par Plasma |
| Tactile (ST FTM5 et Goodix GT9916) | ✅ | coins du bas corrigés (`diting-touch-cmd grip off`) ; SPI à 1 MHz au lieu de 12 |
| GPU Adreno 730 | 🟡 | freedreno + Turnip ; blocages GMU rares (écran noir), à corriger |
| Fluidité de Plasma Mobile | 🟡 | moins fluide qu'Android ; à faire : rendu KWin en basse résolution, agrandi par le contrôleur d'affichage |
| **Connectivité** | | |
| Wi-Fi | ✅ | jusqu'à HE 160 MHz ; économie d'énergie désactivée |
| Bluetooth | 🟡 | appairage et musique (A2DP) vérifiés avec un casque, via PipeWire ; reste l'audio des appels (HFP), non testé : Android le fait passer par SLIMbus (`btfmslim`), le kit mains libres en appel pourrait rester muet |
| USB périphérique (SSH par USB) | ✅ | réseau NCM, `172.16.42.1` |
| USB hôte (OTG) | ✅ | patch `0915` : connecteur USB-C sous pmic-glink, rôle choisi par UCSI ; testé avec un téléphone Android en câble C-C (énuméré en haute vitesse) |
| USB 3, sortie vidéo par USB-C | ⚪ | absents (limite matérielle) : le PHY combo USB3/DisplayPort (`ssphy@88e8000`) est **désactivé dans le DT Android lui-même**, aucun étage SuperSpeed câblé au connecteur. Le port est USB 2.0 ; rien à réactiver côté Linux |
| Casque sur adaptateur USB-C (FSA4480) | ⬜ | pilote `fsa4480` disponible, nœud DT à écrire |
| GPS / GNSS | 🟡 | service LOC du modem présent (`gps-raw`, `gps-nmea`, A-GPS) mais `LOC start` refusé (`GeneralError`), y compris par `qmicli` seul. Écarté : verrou du moteur (était `all`, mis à `none` avec `qmicli --loc-set-engine-lock=none`, gardé après redémarrage du modem), mode (`standalone`/`default`), heure, données d'assistance, déclaration `gps/gps_service` de pd-mapper. Suite : journal F3 du modem par DIAG |
| NFC | ❌ | NXP SN1xx, non gérée par le pilote `nxp-nci` |
| Émetteur infrarouge | ✅ | patch `0913` (`ir-spi`, `/dev/lirc0`) ; émission vérifiée (`ir-ctl -S nec:…`, LED vue par une autre caméra) |
| **Téléphonie** | | |
| Modem, 4G, SMS, USSD | ✅ | avec les correctifs ModemManager (`pkgbuilds/modemmanager-fix/`) |
| Données mobiles | ✅ | idem |
| Appels | 🟡 | en 3G uniquement : pas de VoLTE |
| **Audio** | | |
| Haut-parleurs stéréo | ✅ | protection CS35L41 avec la calibration d'usine |
| Écouteur, micros | ✅ | |
| Audio des appels | ✅ | écouteur, haut-parleur, sourdine |
| **Capteurs** | | |
| Rotation, proximité, lumière, boussole, gyroscope | ✅ | via le SLPI (`libssc`, `iio-sensor-proxy-diting`) |
| Écran éteint contre l'oreille, lever pour réveiller | ✅ | `diting-proximite-appel`, `diting-lever-reveil` |
| Lecteur d'empreintes | 🟡 | Goodix sous l'écran, tout passe par l'appli sécurisée `gfdit` (pilote QSEECOM, patchs `0918`, `0920`, `0922`, `0923`) : capteur alimenté, détecté, initialisé ; disque lumineux sous le capteur (HBM local, fichier `local_hbm`) ; **images du doigt capturées** (crêtes visibles). Bloqué à l'enrôlement, lié à la chaîne keymaster/gatekeeper d'Android. Voir [extra/empreintes](extra/empreintes/README.md) |
| **Caméras** | | |
| Principale (S5KHP1, 200 Mpx) | ✅ | autofocus par contraste (recherche locale, toucher l'aperçu pour choisir le sujet, appui long pour revenir au centre), photo dans plasma-camera ; ❌ sur les unités à capteur Samsung HM6 |
| Ultra grand-angle (S5K4H7) | ✅ | diting-ubuntu |
| Frontale (IMX596), macro (GC02M1) | ✅ | 30 im/s |
| ISP matériel (IPE/BPS), JPEG matériel | ❌ | aucun pilote Linux : libcamera traite les images sur le CPU (ISP logiciel) |
| Flash arrière / lampe torche | ✅ | patch `0912`, LED `white:flash` |
| **Vidéo** | | |
| Décodage matériel (H.264, HEVC, VP9) | ✅ | VA-API |
| Encodage matériel | ❌ | le firmware se bloque au démarrage du flux |
| **Divers** | | |
| Vibreur | ✅ | patch `0904` |
| Batterie, charge | 🟡 | niveau, courant, état de charge dans UPower ; pas de limite de charge |
