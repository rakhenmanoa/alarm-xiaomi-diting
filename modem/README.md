# Modem du Xiaomi 12T Pro (diting) — tout ce qui a été fait

Ce dossier regroupe tout ce qui concerne le modem du diting sous Arch Linux ARM : le démarrage du
processeur modem (MPSS), la liaison data (IPA), les services Qualcomm (rmtfs, tqftpserv, qrtr), la
préparation de la SIM, ModemManager et ses deux correctifs, les droits pour les appels et SMS, et
les données mobiles 4G.

> **Ce sont des copies de référence, figées le 2026-10-31.** Les fichiers réellement construits
> et installés restent ceux de `pkgbuilds/` (`linux-diting`, `diting-device`, `qrtr`, `rmtfs`,
> `tqftpserv`, `modemmanager-fix`) et de royka1/diting-ubuntu au commit `c68ff47db947`. Après une
> modification, mettez d'abord ces originaux à jour, puis recopiez ici.

## Contenu

| Dossier | Contenu | Original |
|---|---|---|
| `patches/diting-ubuntu/` | Patches noyau du modem (`0029`) et de la liaison data IPA v5.1 (`0030`–`0043`) | `kernel/patches/` de diting-ubuntu |
| `patches/modemmanager/` | `0001` : plantage netlink ; `0002` : client data lié à l'abonnement primaire | `pkgbuilds/modemmanager-fix/` |
| `scripts/diting-ubuntu/` | `diting-modem-setup` (SIM et technologies radio), son service, et le réglage « quick suspend/resume » de ModemManager | `ubuntu-diting/packages/device-xiaomi-diting/` |
| `scripts/alarm-diting/` | Règle PolicyKit pour les appels, SMS et USSD | `pkgbuilds/diting-device/diting-modemmanager.rules` |
| `paquets/` | PKGBUILD de `qrtr`, `rmtfs` et `tqftpserv`, utilisés seulement si ALARM ne les fournit pas | `pkgbuilds/{qrtr,rmtfs,tqftpserv}/` |
| `outils/qmi-dds-test.py` | Diagnostic des sessions data en QMI, sans ModemManager | `extra/qmi-dds-test/` |
| `docs/resume-donnees-mobiles-diting.txt` | Résumé texte du problème `no-service` et de sa solution | — |

Pas copiés, car ils viennent du téléphone et ne doivent pas être publiés : l'image du modem
(`modem.mbn`), le firmware GSI de l'IPA (`ipa_fws.mbn`), les profils opérateur (`modem_pr/`), et
les sauvegardes `modemst1`, `modemst2`, `fsg` et `fsc` (IMEI, calibration radio). Les trois premiers
sont fournis par le paquet local `firmware-xiaomi-diting`, dans
`/usr/lib/firmware/qcom/sm8475/xiaomi/diting/`.

## 1. Matériel

Le modem est **intégré au SoC** SM8475 : ce n'est ni un module USB ni un module PCIe. Le noyau le
démarre comme un remoteproc (MPSS), les échanges de contrôle passent par **QRTR** (`qrtr://0`), et
les paquets de données par l'**IPA** v5.1 (`rmnet_ipa0`, multiplexé en `qmapmux0.N`).

Le téléphone est double SIM : un emplacement physique (1) et l'eSIM (2). Dans ce téléphone, la seule
SIM active est l'eSIM Airtel (64601), dans l'emplacement 2.

## 2. Noyau

| Patch | Origine | Rôle |
|---|---|---|
| `0029` | diting-ubuntu | DT : remoteproc MPSS avec l'image du vendeur (`modem.mbn`, aplatie), et la mémoire partagée rmtfs (`qcom,rmtfs-mem`, 0x280000, client-id 1) où le modem lit son EFS. |
| `0030` | diting-ubuntu (XEC) | Pilote `ipa` : prise en charge initiale de l'IPA v5.1 (SM8450). |
| `0031` | diting-ubuntu (XEC) | DT SM8450 : nœud IPA. |
| `0032`–`0040` | diting-ubuntu (XEC) | Données IPA v5.1 : ressources, groupes, adresse IMEM, bande passante, régions mémoire (statistiques, tethering, drop). |
| `0041` | diting-ubuntu | Binding DT `qcom,sm8450-ipa`. |
| `0042` | diting-ubuntu | Interruption IPA sur SPI 654 (et non 655, recopiée du SM8350), comme dans les DT du vendeur. |
| `0043` | diting-ubuntu | DT du diting : active l'IPA avec le firmware GSI signé du vendeur (`ipa_fws.mbn`), chargé dans `ipa_gsi_region@8b910000`. |

Les données IPA ont été écrites pour le SM8450 (OnePlus 10 Pro). Pour le SM8475, elles n'ont été
vérifiées qu'avec le DT du vendeur, pas avec son pilote `ipa3`.

Vérifier que le modem tourne et que la liaison data existe :
```sh
grep . /sys/class/remoteproc/*/name /sys/class/remoteproc/*/state
```
```sh
ip -br link | grep -iE "rmnet|qmap"
```

## 3. Services Qualcomm

| Service | Rôle |
|---|---|
| `rmtfs` | Sert au modem son système de fichiers (EFS), à partir des partitions `modemst1`, `modemst2` et `fsg`. Lancé avec `-r -P -s` : **lecture seule** (`-r`), et c'est lui qui démarre le modem (`-P`). |
| `tqftpserv` | Serveur TFTP sur QRTR : le modem y lit sa configuration et les profils opérateur (`modem_pr/`). |
| `qrtr` | Bibliothèque et outils QRTR (`qrtr-lookup`). |

Les trois sont activés par `diting.preset`. Les PKGBUILD de `paquets/` ne servent que si le dépôt
ALARM ne fournit pas le paquet (`scripts/20-construire-paquets.sh`).

**rmtfs en lecture seule** : rien de ce que le modem écrit dans son EFS n'est conservé. C'est voulu,
pour ne jamais abîmer l'IMEI ni la calibration, mais tout réglage du modem doit donc être réappliqué
à chaque démarrage. C'est le rôle de `diting-modem-setup`.

## 4. Préparation de la SIM — `scripts/diting-ubuntu/diting-modem-setup`

Service `oneshot`, lancé après rmtfs et tqftpserv et **avant ModemManager** :

1. attend l'apparition du service UIM du modem (QMI 11) sur QRTR, 60 s au plus ;
2. si aucune session de provisionnement primaire n'existe, active la première application USIM du
   premier emplacement où une carte est présente (`--uim-change-provisioning-session`). Sans cela,
   ModemManager échoue avec « GW primary session index unknown ». C'est l'équivalent de
   `msm-modem-uim-selection` de postmarketOS ;
3. autorise toutes les technologies radio (`gsm|umts|lte|5gnr`) : par défaut, la NV du modem n'a que
   le GSM.

Ici, l'emplacement 1 est vide (`no-atr-received`), donc c'est l'eSIM de l'emplacement 2 qui devient
l'abonnement primaire :
```sh
sudo qmicli -d qrtr://0 --uim-get-card-status
```
→ `Primary GW: slot '2', application '1'`.

## 5. ModemManager

### Réglage installé — `scripts/diting-ubuntu/10-diting-modemmanager.conf`
Va dans `/usr/lib/systemd/system/ModemManager.service.d/` et lance ModemManager avec
`--test-quick-suspend-resume`. Le modem reste allumé pendant la veille. Par défaut, ModemManager
supprime les modems à la mise en veille et les recherche au réveil, mais il ne retrouve pas le modem
QRTR (« couldn't check support for device 'qcom-soc' »). Données et appels disparaissaient donc
après la première veille. Avec ce réglage, il garde l'objet modem et resynchronise seulement son
état au réveil.

### Correctif `0001` — plantage netlink (2026-10-26)
ModemManager 1.24.2 d'Arch plantait (SIGSEGV) toutes les 20 à 30 s en recréant la liaison de
données : `transaction_complete()` lisait `tr->completion_fn` après avoir libéré la transaction
(saut vers l'adresse `0x30`). La 4G coupait, et plasma-dialer perdait le modem pendant les appels.
C'est le commit officiel 9317cbc4 (2026-10-04), le même que diting-ubuntu.

### Correctif `0002` — données mobiles, `cm error: no-service` (2026-10-30)
**Symptôme** : le modem s'enregistre (`registered`, `lte`, `packet service state: attached`, signal
à 73 %), mais toute session data échoue aussitôt :
```
couldn't start IPv4 network: QMI protocol error (14): 'CallFailed'
verbose call end reason (3,2001): [cm] no-service
```

**Écarté** : APN (`internet` est correct, c'est aussi celui du bearer initial), type d'IP (échec
identique en IPv4 seul), profils du modem (1, APN vide, et 4, `qdp_profile_ia`), mode réseau
préféré, provisionnement de la SIM (eSIM bien primaire), liaison noyau (`rmnet_ipa0` présent). Le
journal DEBUG montre une requête `Start Network` correcte, refusée par le modem lui-même.

**Cause** : sur ce modem multi-SIM, un client data (WDS) doit être lié à un abonnement. ModemManager
ne le fait jamais, et son client reste sur `any`. Le modem envoie alors la session vers l'abonnement
data désigné laissé par Android, qui n'est pas celui où la SIM est provisionnée sous Linux.

**Correctif** : juste après la liaison du client WDS au canal data (mux), en IPv4 comme en IPv6,
ModemManager envoie `Bind Subscription = primary` (message WDS `0x00AF`, connu de libqmi depuis la
1.37). Un refus du modem n'est pas bloquant : avertissement dans le journal, puis la suite normale.
Valable pour une seule SIM active, physique ou eSIM. Avec deux SIM, il faudrait rendre ce choix
configurable.

**Preuve sans recompiler** : `outils/qmi-dds-test.py` (voir la section 8).

### Recompiler ModemManager avec les deux correctifs

Les correctifs ne sont pas empaquetés : on recompile le paquet d'Arch sur le téléphone, une commande
à la fois.
```sh
git clone https://gitlab.archlinux.org/archlinux/packaging/packages/modemmanager.git ~/modemmanager
```
```sh
cd ~/modemmanager
```
```sh
git checkout 1.24.2-2
```
```sh
makepkg -soA
```
Si la vérification PGP échoue (clé `734614319E26A433` inconnue) : `gpg --import keys/pgp/*.asc`,
puis relancer `makepkg -soA`.
```sh
patch -d src/ModemManager -p1 < ~/alarm-xiaomi-diting/modem/patches/modemmanager/0001-netlink-fix-use-after-free-calling-completion_fn-on-.patch
```
```sh
patch -d src/ModemManager -p1 < ~/alarm-xiaomi-diting/modem/patches/modemmanager/0002-bearer-qmi-bind-WDS-client-to-primary-subscription.patch
```
```sh
makepkg -eA --nocheck
```
```sh
sudo pacman -U modemmanager-1.24.2-2-aarch64.pkg.tar.zst libmm-glib-1.24.2-2-aarch64.pkg.tar.zst
```
```sh
sudo systemctl restart ModemManager
```

Pour que `pacman -Syu` ne les remplace pas, dans `/etc/pacman.conf`, section `[options]` :
```
IgnorePkg = modemmanager libmm-glib
```

## 6. Appels, SMS et USSD — `scripts/alarm-diting/diting-modemmanager.rules`
Installée dans `/usr/share/polkit-1/rules.d/50-diting-modemmanager.rules` par `diting-device`
(0.1.6). Sans elle, PolicyKit refuse à la session (plasma-dialer, Spacebar, `mmcli`) les actions
`org.freedesktop.ModemManager1.*`, dont `Voice`. Elle les autorise pour le groupe `wheel`.

L'audio des appels (ADSP, `diting-voice-call`, `diting-callaudio`) est décrit dans
[audio/README.md](../audio/README.md).

## 7. Données mobiles avec NetworkManager

Connexion `Airtel` (type `gsm`, APN `internet`). Une fois le correctif `0002` en place :
```sh
sudo nmcli connection modify Airtel gsm.apn internet connection.autoconnect yes
```
```sh
sudo nmcli connection up Airtel
```

Vérifier :
```sh
nmcli device | grep -i gsm
```
→ `qrtr0  gsm  connecté  Airtel`.
```sh
ip -br addr show qmapmux0.0
```
```sh
ping -c3 -I qmapmux0.0 1.1.1.1
```

Pour une autre SIM, adapter l'APN (Telma : `telma.net`). Plasma Mobile affiche alors **deux barres
de réseau** : le signal du modem (ModemManager) et la connexion internet active par la 4G
(NetworkManager, icône `network-mobile`). C'est normal, avec un seul modem (`mmcli -L`).

### Partage de connexion en Wi-Fi
```sh
sudo pacman -S --needed dnsmasq
```
```sh
sudo nmcli device wifi hotspot ifname wlp1s0 con-name Hotspot ssid diting-4G band bg password "MotDePasse"
```
Ensuite : `sudo nmcli connection up Hotspot` ou `down Hotspot`. La puce Wi-Fi ne peut pas être
connectée à un réseau et servir de point d'accès en même temps. Si le Wi-Fi a été coupé
(`nmcli radio wifi off`), l'activation échoue avec « device is not available » :
`nmcli radio wifi on`.

## 8. Diagnostic — `outils/qmi-dds-test.py`

En QRTR, un client WDS ne survit pas entre deux appels de `qmicli`. On ne peut donc pas enchaîner
`--wds-bind-mux-data-port` et `--wds-start-network`, et le passage par `qmi-proxy` (`-p`) dépasse le
délai. qmicli 1.38 n'a pas non plus d'option pour `Bind Subscription`. Ce script (Python + libqmi
GObject introspection) fait tout dans un seul processus.

```sh
sudo python3 qmi-dds-test.py lire
```
Ne modifie rien : affiche l'abonnement du client (`any` sans liaison).
```sh
sudo python3 qmi-dds-test.py test primary
```
Lie un client temporaire au canal data (mux 5, à l'écart de ceux de ModemManager), puis à
l'abonnement choisi (`primary`, `secondary` ou `default`). Ouvre ensuite une session IPv4 sur l'APN
`internet` et la referme. En cas de refus, affiche la raison détaillée. Rien de persistant n'est
modifié dans le modem.

Autres commandes utiles :

| Commande | Ce qu'elle montre |
|---|---|
| `mmcli -m any` | État, technologie, signal, opérateur, `packet service state`, bearer initial |
| `sudo qmicli -d qrtr://0 --dsd-get-system-status` | Système data disponible vu par le modem (LTE ici) |
| `sudo qmicli -d qrtr://0 --wds-get-profile-list=3gpp` | Profils data du modem |
| `sudo mmcli -G DEBUG` puis `journalctl -u ModemManager` | Messages QMI échangés (`sudo mmcli -G INFO` pour revenir) |
| `coredumpctl list ModemManager` | Plantages de ModemManager |

Les messages `Handover signaled, but it already happened` du noyau viennent du SLPI (capteurs), pas
du modem. Ils sont sans gravité.

## 9. Pistes ouvertes
- Rendre configurable l'abonnement du correctif `0002` (deux SIM actives).
- L'abonnement data désigné du modem n'est pas modifié (libqmi n'a pas de message pour cela). Le
  correctif contourne le problème client par client.
- Veille : sur batterie, les indications QMI du modem (état du réseau) réveillent le téléphone de
  temps en temps. ModemManager ne demande pas au modem de les couper écran éteint, contrairement à
  Android.
- Empaqueter ModemManager corrigé (PKGBUILD dans `pkgbuilds/`) au lieu de le recompiler à la main.
