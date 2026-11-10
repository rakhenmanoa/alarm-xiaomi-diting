# Lecteur d'empreintes (étude de faisabilité)

Capteur Goodix optique sous l'écran. Rien ne passe par Linux : le bus SPI du
capteur appartient à la TrustZone, et une appli sécurisée Goodix (`gfdit`)
fait la capture, l'enregistrement et la comparaison. Android lui parle depuis
son HAL (`libgf_hal.so`) par l'interface QSEECOM.

## État (2026-11-06)

Fonctionne, piloté depuis Linux (voir `qseecom-cli.c`) :

- Chargement de `gfdit` dans la TrustZone (QSEECOM) et dialogue par
  commandes brutes, avec vrais statuts de retour.
- Alimentation du capteur, reset, détection, initialisation, réglage du
  temps de pose.
- Éclairage local sous l'écran (HBM local) pendant la capture.
- **Capture normale fonctionnelle** : images brutes du capteur, crêtes du
  doigt visibles après traitement.

Bloqué, par conception, pas par un oubli :

- L'**enrôlement/reconnaissance natifs Android** (`hw_auth_token_t` signé)
  exigent une clé HMAC que `gfdit` reçoit scellée par `keymaster64` et ne
  révèle jamais à Linux ; `gfdit` vérifie la signature du jeton avec cette
  clé réelle, jamais avec une clé de test. Toute la chaîne (identité →
  scellement → décapsulation → vérification) a été vérifiée de bout en
  bout et fonctionne exactement comme prévu par Qualcomm/Android — elle ne
  laisse aucune porte dérobée côté Linux.
- Une piste `gatekeeper` (fusionné dans `keymaster64` sur cet appareil) a
  été explorée pour contourner ce mur, mais bute sur un format de requête
  interne propriétaire de Qualcomm, non documenté publiquement ; analyse
  arrêtée par prudence avant tout appel réel (écriture probable en
  stockage sécurisé persistant).

## Prochaines étapes

1. **Reconnaissance côté Linux**, indépendante d'Android, à partir des
   images brutes — voie recommandée. Chaîne écrite dans `reco/`
   (`gfreco.py`, `gf-capture.sh`), mais **mesurée sans pouvoir séparer deux
   doigts** avec la capture d'usine `1003/21` (EER 47 %, hasard) : la
   variabilité d'un appui à l'autre dépasse 17 fois la différence entre
   doigts. Prochaine étape : passer à la capture normale `1002/3` (mode 7)
   et relancer `gfreco.py diag` (détails : `reco/README.md`).
2. Si l'envie revient : reprendre la piste `gatekeeper` (format de requête
   propriétaire à décoder), en analyse statique uniquement avant toute
   requête réelle sur le téléphone.

## Alimentations du capteur (2026-11-08)

Le capteur a besoin de **trois** alimentations, qu'aucun pilote Linux
n'active : **L1C** (1,8 V, entrées-sorties), **L7C** (3,0 V) et **L11C**
(2,504 V). Avec L7C et L11C seules : `1002/0` → 1043, `1002/1` → 1064
(même schéma que reset à 0). L1C ajoutée (`fdtput … ldo1
regulator-always-on`) : `1002/0` et `1002/1` → **0**, détection 8 / 224 /
192. Le patch noyau `0922` (pkgrel 30) garde les trois allumées.
Explication probable des résultats changeants du 4 au 7 novembre : selon les
essais, l'une ou l'autre de ces alimentations restait allumée par le
bootloader ou par un module de test.

Vérifier :

```sh
sudo grep -E "l1c|l7c|l11c" /sys/kernel/debug/regulator/regulator_summary
```

## Listeners pour la calibration (2026-11-08)

`gfdit` lit sa calibration d'usine via les listeners de qseecomd (absents
sous Linux) ; sans eux elle prend des valeurs par défaut.

- Avec le patch `0924` (inscription smcinvoke), la TrustZone redémarre le
  téléphone dès qu'elle doit transmettre une demande : patch retiré
  (linux-diting pkgrel 31, inscription classique).
- Tampons à la taille de Qualcomm (minkipc) : fichiers `0xa` 20 Ko,
  fichiers GP `0x7000` 504 Ko, RPMB `0x2000` 28 Ko.
- Avec pkgrel 31 et
  `sudo diting-qseecomd -v -e 0xa:20480 -e 0x7000:516096`, plus de
  plantage ; première demande reçue pendant `1002/9` : service `0xa`,
  commande `0x202` (FILE_OPEN) de `/mnt/vendor/persist/goodix/cali_data.so`.
- `persist` contient bien la calibration d'usine de 2022
  (`/mnt/vendor/persist/goodix/` : `cali_data.so`, `caliParamsInfo.so`,
  `chartbase.so`, `highflesh.so`, `midbase.so`, `oemInfo.so`…).
- diting-qseecomd 0.2.x : option `-f`, services fichiers `0xa` et fichiers
  GP `0x7000` (504 Ko) en lecture seule, limités à `/mnt/vendor/persist/`
  (écritures refusées avec EROFS).
- Pendant `1002/9`, gfdit ouvre puis ferme `cali_data.so` par `0xa`, puis le
  lit par `0x7000` avec la commande `0` (ancienne lecture à chemin absolu,
  absente de minkipc, qui commence à 4).
- Avec diting-qseecomd 0.2.1 (`-v -f`) : gfdit lit `cali_data.so` en entier
  (4 octets d'en-tête, puis 512000 + 12340 octets), sans erreur. Aucun autre
  fichier pendant `1002/9`.
- Chaîne complète de `gf-capture.sh` (jusqu'à `1003/18`) : gfdit lit en plus
  `caliParamsInfo.so`, `BMatrix.so`, `HLBMatrix.so`, `highflesh.so` (+ `.bak`)
  et `midbase.so` (+ `.bak`), tous servis sans erreur (`chartbase.so`,
  `oemInfo.so`, `gf_extra_feature.so` ne sont pas demandés).
- `1003/38` à 86080 octets après cette chaîne : statut 1028
  (`GF_ERROR_SPI_RAW_DATA_CRC_FAILED`) au lieu de 1004 (`BAD_PARAMS`) ;
  juste après `1002/0`, le changement de taille du tampon fait refuser l'appel
  par la TZ (`scm call failed with error -22`).
- `1003/17` (86080 octets, pose 50, gain 1,0) : statut 0, image 192 × 224
  **brute** (même allure que `1003/21` : grille de points, tache du HBM) ;
  la calibration ne s'applique pas aux commandes d'usine.
- **`1002/3` en mode 1** (`GF_CAP_OP=1`, mode de l'enrôlement Android) après
  la chaîne de `gf-capture.sh` : **statut 0**, alors qu'il donnait toujours
  1028 le 2026-11-04 (sans calibration). Même chaîne **sans**
  diting-qseecomd (2026-11-10) : **1028**. C'est donc la calibration d'usine
  qui débloque la capture normale : diting-qseecomd `-f` est indispensable.
- diting-qseecomd 0.3.0 : `mnt-vendor-persist.mount` (persist en lecture
  seule) et service `diting-qseecomd -f` (sans `-w`), à activer avec
  `systemctl enable --now diting-qseecomd`. Vérifié : `1002/3` mode 1 donne
  0 avec le service lancé par systemd.
- `1001/1` (enrollImage, 48 octets) juste après cette capture, services
  inscrits : toujours `invoke : Invalid argument` (refus de la TZ avant
  l'appli), et **aucune** demande de fichier en plus. Le mur de
  l'enrôlement ne vient donc pas d'un listener manquant.
- Analyse de `gf_algo_enroll` (gfdit 0x5ba8) : rejette tout de suite avec
  1024 (`INVALID_DATA`) si la requête fait moins de 44 octets, sinon envoie
  l'évènement 18 (`validate_token`). Confirmé sur le téléphone : `1001/1` à
  40 o → 1024 (contrôle interne), à 44 et 48 o → `Invalid argument` (refus de
  la TZ en amont). Les deux contraintes sont **incompatibles** par ce chemin.
- Conclusion : `1001/1` (`enrollImage`) ne se pilote pas seul. L'enrôlement
  réel est une séquence `1000/2` (preEnroll) → `1000/3` (enroll, jeton
  `hw_auth_token_t`) → capture → `1001/1`, et `1000/3` ouvre la session ;
  sans elle, la TZ refuse `1001/1`. `1000/3` exige le jeton signé par
  keymaster (produit sous Android). Mur keymaster confirmé, pas un détail de
  taille ni un listener manquant.

## Rappels pratiques

- Après chaque redémarrage : `sudo gpioset -z -c gpiochip6 41=1` (reset du
  capteur), sinon 1012/2038 sur les commandes d'usine.
- Chargement de `gfdit` : ~15 Mo contigus ; si « Cannot allocate memory »,
  redémarrer ou ajouter `cma=128M` à `/etc/diting/cmdline`.
- Après des heures de marche, le tampon de 4 Mo (`--goodix-carveout`) peut
  être refusé (mémoire fragmentée, rapport « Node 0 Normal: … 0*4096kB »
  dans `dmesg`) : `sync`, `echo 3 > /proc/sys/vm/drop_caches`,
  `echo 1 > /proc/sys/vm/compact_memory`, puis relancer.
- Changement d'initramfs ou de DT : `mkinitcpio -P` **puis**
  `diting-install-bootimg` (les deux).
- Autres chantiers ouverts, sans lien : recharge limitée (retest 50–70 %),
  HBM global (patch 0917), Bluetooth HFP, FSA4480 (casque USB-C), NFC,
  ISP/JPEG, GPS (journaux F3 du modem).
- Matériel non publiable (hors dépôt, à refaire depuis le téléphone) :
  `gfdit.mbn`, `libgf_hal.so`, désassemblages, partition keymaster.

## Consignes de travail de Fanasina

- Répondre toujours en **français**.
- Donner les commandes **une par une**, pas de script tout fait.
- Ne **jamais** publier de blob propriétaire (TA `.mbn`, HAL, partitions
  extraites) ni les images d'empreintes de Fanasina.
- La recherche détaillée (désassemblages, essais, hypothèses) est
  conservée **localement**, hors dépôt ; ce README reste minimal. Partir
  des notes locales pour reprendre le travail.
