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
| `pilote/drivers/net/ipa/` | Pilote IPA complet (liaison data du modem) **après** application de la série (Linux 7.2.10 + diting-ubuntu `c68ff47`, patches `0030` et `0032`–`0040`). Compilable seul, voir la section 10. | `drivers/net/ipa/` du noyau patché |
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

**Preuve sans recompiler** : `outils/qmi-dds-test.py` (voir la section 8). Le déroulé complet de
l'enquête est dans la section 9.4.

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

## 9. Problèmes rencontrés, dans l'ordre

### 9.1 « No modems were found » au premier démarrage
`mmcli -L` ne trouvait aucun modem juste après le démarrage. Le modem démarre en cascade : rmtfs
lance le MPSS (`-P`), le modem publie ses services sur QRTR, puis `diting-modem-setup` active la SIM
avant que ModemManager ne le détecte. Il faut compter une bonne minute après le démarrage. Quelques
instants plus tard : `/org/freedesktop/ModemManager1/Modem/0 [QUALCOMM INCORPORATED]`, inscrit en 4G
sur Airtel, IMEI intact (lu depuis les partitions `modemst` sauvegardées).

Si le modem n'apparaît toujours pas, contrôler la chaîne dans l'ordre :
`/sys/class/remoteproc/*/state` (`running`), `systemctl status rmtfs` (option `-P`),
`ls /dev/qcom_rmtfs_mem*`, puis `qrtr-lookup` (le modem publie de nombreux services).

### 9.2 ModemManager plante toutes les 20 à 30 s (2026-10-26)
**Symptômes** : la 4G coupe sans cesse, `ModemManager.service: Failed with result 'core-dump'`,
8 plantages en 3 minutes (`coredumpctl list ModemManager`), saut vers l'adresse `0x30`. Le modem
disparaissait aussi de plasma-dialer pendant les appels.

**Cause** : un use-after-free dans `mm-netlink.c` de la 1.24.2. Chaque nouvelle tentative de connexion
data crée et supprime des liens réseau, et c'est là que ModemManager plante. Les tentatives étaient
très fréquentes parce que les connexions échouaient déjà avec `no-service` (section 9.4) : les deux
problèmes s'alimentaient.

**Solution** : correctif `0001` (commit officiel 9317cbc4). Deux obstacles pendant la recompilation :
- `makepkg` répond « modemmanager is not available for the 'aarch64' architecture » : le PKGBUILD
  d'Arch ne déclare que `x86_64`, d'où l'option `-A` ;
- « unknown public key 734614319E26A433 » : `gpg --import keys/pgp/*.asc` dans le dossier du PKGBUILD.

### 9.3 Appels et USSD refusés à plasma-dialer (2026-10-26)
PolicyKit refusait `org.freedesktop.ModemManager1.Voice` à la session. Corrigé par la règle de la
section 6 (`diting-device` 0.1.6).

Fausse piste à retenir : un peu plus tard, les appels sortants sonnaient une fraction de seconde
chez le correspondant puis coupaient (`dialing`, `ringing-out`, puis `terminated` dans la même
seconde ; en QMI, `alerting` puis `disconnecting`). Cause de fin décodée : 156 `NORMAL_UNSPECIFIED`,
cause réseau 31. Ni la 4G (la 3G forcée ne changeait rien), ni le service voix (arrêté, l'appel coupait
encore), ni plasma-dialer n'étaient en cause : **le crédit d'appel était épuisé**. Avant de chercher
plus loin sur un appel coupé par le réseau, vérifier le solde par USSD
(`mmcli -m any --3gpp-ussd-initiate="CODE"`).

### 9.4 Données mobiles impossibles avec la SIM 2 : `cm error: no-service` (2026-10-30)

C'est le problème qui a demandé de patcher **ModemManager**. NetworkManager n'est pas en cause : il
se contente de demander la connexion à ModemManager, et c'est ModemManager qui parle au modem (QMI).
Il n'a donc pas fallu toucher à NetworkManager.

**Situation** : une seule SIM, l'eSIM Airtel, qui occupe l'**emplacement 2**. L'emplacement 1 (tiroir
physique) est vide.

**Symptômes** : appels et SMS fonctionnent. Le modem est enregistré en 4G, mais aucune connexion
internet ne s'établit. NetworkManager relance en boucle (bearers 45, 46, 47…), et chaque tentative
échoue dans la seconde :
```
couldn't start IPv4 network: QMI protocol error (14): 'CallFailed'
verbose call end reason (3,2001): [cm] no-service
couldn't connect bearer: Call failed: cm error: no-service
```
Le message trompe : `no-service` laisse croire à une absence de réseau, alors que `mmcli -m any`
montre `state: registered`, `access tech: lte`, `signal quality: 73%`, `registration: home` et
`packet service state: attached`. La couche radio va bien. C'est l'ouverture de la session data qui
est refusée par le gestionnaire d'appels interne du modem (`cm`, type 3, code 2001).

**L'enquête, étape par étape**

| Étape | Test | Résultat | Conclusion |
|---|---|---|---|
| 1 | `mmcli -m any` : état réseau | `attached` en LTE, mode préféré `2g` | Radio correcte. Mode préféré corrigé en 4G (`--set-allowed-modes="2g\|3g\|4g\|5g" --set-preferred-mode=4g`), sans effet sur l'erreur. |
| 2 | Bearer initial (section « 3GPP EPS ») | APN `internet`, `ipv4v6` | L'attache 4G utilise déjà le bon APN : le réseau l'accepte. |
| 3 | Connexion NetworkManager `Airtel` | APN `internet` | Pas d'erreur d'APN côté NetworkManager. |
| 4 | `--simple-connect="apn=internet,ip-type=ipv4"`, autoconnect coupé | `no-service` | Ni l'APN ni le type d'IP. |
| 5 | `--simple-connect="profile-id=1"` | `no-service` | Profil 1 : APN vide. |
| 6 | Piste double SIM, première version : « la data part vers l'emplacement 1 » | `qmicli --uim-get-card-status` : `Primary GW: slot '2'` | L'eSIM est bien l'abonnement **primaire**. Piste écartée sous cette forme. |
| 7 | `qmicli --dsd-get-system-status` | `3gpp-lte`, système préféré | Le module data du modem voit bien la LTE. |
| 8 | `qmicli --wds-get-profile-list=3gpp` | Profil 4 `qdp_profile_ia`, APN `internet` (`ia` = *initial attach*) | — |
| 9 | `--simple-connect="profile-id=4"` | `no-service` | Même le profil d'attache échoue. |
| 10 | `qmicli --wds-start-network` direct | `InvalidOperation` | Sur ce modem, il faut d'abord lier le client WDS au canal data (mux). |
| 11 | Liaison mux puis démarrage, en deux appels de qmicli | « Unknown client 1 for service wds » | En QRTR, le client WDS disparaît à la fin de chaque processus `qmicli`. |
| 12 | Même chose via `qmi-proxy` (`-p`) | « Transaction timed out » | Le proxy n'est pas utilisable ici en QRTR. |
| 13 | Journal DEBUG de ModemManager | Requête `Start Network` correcte (APN `internet`, IPv4, `lte`), réponse `CallFailed` | Le refus vient du **modem**. Liaison noyau présente (`rmnet_ipa0` `UP`). |
| 14 | Options de qmicli 1.38 sur les abonnements | Seulement `--wds-bind-data-port` et `--wds-bind-mux-data-port` | Pas d'option pour `Bind Subscription`, même si libqmi connaît le message depuis la 1.37. |
| 15 | `outils/qmi-dds-test.py lire` | Abonnement du client : `any` | ModemManager ne lie jamais ses clients data à un abonnement. |
| 16 | `outils/qmi-dds-test.py test primary` | **SESSION DATA OUVERTE** | Cause trouvée. |

**Cause** : le modem est multi-SIM (deux emplacements, plusieurs abonnements). Pour qu'une session data
parte sur la bonne SIM, le client WDS doit être lié à un abonnement. ModemManager 1.24.2 ne le fait
jamais, et son client reste sur `any`. Le modem choisit alors lui-même l'abonnement data, sans doute
d'après le réglage laissé par Android (avec la SIM dans l'emplacement 2), qui ne correspond pas à
l'abonnement primaire où Linux a provisionné l'eSIM. Sur cet abonnement, il n'y a « aucun service »,
d'où `no-service`. Probablement, le problème n'apparaît pas avec une SIM dans l'emplacement 1 (non
vérifié). rmtfs étant en lecture seule, ce réglage du modem ne peut d'ailleurs pas être changé
durablement.

**Solution** : correctif ModemManager `0002`. Dans `src/mm-bearer-qmi.c`, juste après la liaison du
client WDS au canal data (`bind_mux_data_port_ready`), ModemManager envoie maintenant
`Bind Subscription = QMI_SUBSCRIPTION_TYPE_PRIMARY`, pour le client IPv4 comme pour l'IPv6. Si le
modem refuse cette liaison, il note un avertissement (« couldn't bind WDS client to primary
subscription ») et continue comme avant : le patch ne casse rien sur un modem qui ne connaît pas ce
message.

Après recompilation, puis `sudo nmcli connection modify Airtel connection.autoconnect yes` et
`sudo nmcli connection up Airtel` :
```
qrtr0           gsm       connecté      Airtel
qmapmux0.0      rmnet     non-géré      --
```
Un dernier message trompeur : `nmcli connection up Airtel` a d'abord répondu « No suitable device
found for this connection (device usb0 not available…) ». ModemManager venait de redémarrer et le
modem n'était pas encore revenu : NetworkManager ne voyait que `usb0`. Quelques secondes plus tard,
l'autoconnect avait déjà ouvert la connexion.

**Ce qu'il faut retenir**
- `cm error: no-service` avec `packet service state: attached` n'est pas un problème de réseau ni
  d'APN, mais d'abonnement : vérifier d'abord `outils/qmi-dds-test.py lire` puis `test primary`.
- Une mise à jour d'Arch remet un ModemManager sans les correctifs : `IgnorePkg = modemmanager
  libmm-glib`.
- Avec deux SIM actives, le correctif forcerait toujours l'abonnement primaire : il faudrait le rendre
  configurable (section 11).

### 9.5 Deux barres de réseau dans la barre d'état (2026-10-30)
Après le correctif, Plasma Mobile affiche deux indicateurs en forme de barres. Ce n'est pas un second
modem (`mmcli -L` n'en liste qu'un) : la première barre est le signal du modem (ModemManager), la
seconde la connexion internet active par la 4G (NetworkManager, icône `network-mobile`). Avant le
correctif, la data ne se connectait jamais, donc cette seconde icône n'apparaissait pas.
`sudo nmcli connection down Airtel` la fait disparaître.

### 9.6 Point d'accès Wi-Fi : « device is not available » (2026-10-30)
`nmcli device wifi hotspot` échouait parce que le Wi-Fi était resté coupé après un test
(`nmcli radio wifi off`). `nmcli radio wifi on`, puis `sudo nmcli connection up Hotspot`. Voir la
section 7.

## 10. Compiler et installer le pilote IPA sans recompiler le noyau

Le pilote IPA est un module (`CONFIG_QCOM_IPA=m`, fichier `ipa.ko`). Le noyau du diting n'impose ni
signature des modules ni `MODVERSIONS` : un module compilé à part se charge dès que sa version
(`vermagic`) est exactement celle du noyau en service. La méthode générale est décrite dans
[docs/modules-sur-le-telephone.md](../docs/modules-sur-le-telephone.md).

`pilote/drivers/net/ipa/` contient **tout** le pilote, déjà patché. On le compile donc comme un
module externe (`M=`), contre un arbre noyau Linux 7.2.10 **sans aucun patch** : il suffit d'avoir
la configuration du noyau installé. Si vous avez déjà préparé l'arbre patché de
[screen/README.md](../screen/README.md) (section 6), il convient aussi : passez directement à l'étape b.
Tout se fait **sur le téléphone**, une commande à la fois.

Vérifié le 2026-10-31 en compilation croisée (GCC 13, configuration `defconfig` + `diting.config`) :
`ipa.ko` avec `vermagic=7.2.10-diting`, alias `qcom,sm8450-ipa` et données v5.1.

### a. Préparer l'arbre noyau (une seule fois)

Outils :
```sh
sudo pacman -S --needed base-devel bc git
```

Sources à la version du paquet (`_kver` du PKGBUILD de `linux-diting`) :
```sh
git clone --depth 1 --branch v7.2.10 https://github.com/gregkh/linux ~/linux-7.2.10
```
```sh
cd ~/linux-7.2.10
```

Configuration du noyau installé, puis préparation à la compilation de modules :
```sh
cp /usr/share/doc/linux-diting/config .config
```
```sh
make LOCALVERSION= olddefconfig
```
```sh
make LOCALVERSION= -j8 modules_prepare
```

`LOCALVERSION=` (vide) est indispensable : sans lui, l'arbre git ajoute un « + » à la version et le
module est refusé. Les deux lignes suivantes doivent être identiques :
```sh
make -s LOCALVERSION= kernelrelease; uname -r
```

### b. Modifier le pilote

Travailler sur une copie, pour garder la référence intacte :
```sh
cp -a ~/alarm-xiaomi-diting/modem/pilote/drivers/net/ipa ~/ipa
```

Les fichiers propres au diting :

| Fichier | Contenu |
|---|---|
| `data/ipa_data-v5.1.c` | Description de l'IPA v5.1 : endpoints (modem, AP), groupes de ressources, régions mémoire, bande passante, adresse IMEM |
| `ipa_data.h`, `ipa_main.c` | Déclaration des données v5.1 et association à `qcom,sm8450-ipa` |
| `ipa_mem.c`, `ipa_mem.h` | Région `IPA_MEM_MODEM_STATS_DROP` |
| `ipa_reg.c`, `gsi_reg.c` | Choix des registres IPA et GSI pour la v5.1 (elle réutilise ceux de la v5.0) |
| `Makefile` | Ajout de `5.1` à `IPA_DATA_VERSIONS` |

### c. Compiler le module seul
```sh
cd ~/ipa
```
```sh
make -C ~/linux-7.2.10 LOCALVERSION= M=$PWD KBUILD_MODPOST_WARN=1 -j8 modules
```
Compter une à deux minutes. Les avertissements `WARNING: modpost: "…" undefined!` sont normaux (pas
de `Module.symvers` du noyau installé) : les symboles sont résolus au chargement, et `depmod`
retrouve les dépendances (`qmi_helpers`, `qcom_smem`…).

Vérifier la version du module, qui doit être celle de `uname -r` :
```sh
modinfo -F vermagic ipa.ko
```
```sh
modinfo -F alias ipa.ko | grep 8450
```

### d. Installer

Dans `updates/`, qui a priorité sur le module du paquet (l'original reste en place) :
```sh
sudo install -Dm644 -t /usr/lib/modules/$(uname -r)/updates/ ipa.ko
```
```sh
sudo depmod -a
```

Ne pas décharger l'IPA à chaud : le modem tourne en permanence et dialogue avec lui (SMP2P, QMI).
Il faut redémarrer :
```sh
sudo reboot
```

Après le redémarrage, vérifier que c'est bien le module de `updates/` qui est chargé et que la
liaison data revient :
```sh
modinfo -n ipa
```
```sh
sudo dmesg | grep -i ipa
```
```sh
ip -br link | grep -iE "rmnet|qmap"
```
```sh
nmcli device | grep -i gsm
```

Le module n'est pas dans l'initramfs (HOOKS `base udev modconf fsck`) : ni `mkinitcpio -P` ni
`diting-install-bootimg` ne sont nécessaires. Ils ne le deviennent que si l'on modifie aussi le
device tree (patches `0029`, `0031`, `0041`–`0043`, voir `docs/modules-sur-le-telephone.md`,
section 3).

### e. Revenir en arrière

Si la data mobile ne revient pas, se connecter en SSH (USB : `172.16.42.1`, ou Wi-Fi) et retirer le
module de test :
```sh
sudo rm /usr/lib/modules/$(uname -r)/updates/ipa.ko
```
```sh
sudo depmod -a
```
```sh
sudo reboot
```

À faire aussi après l'installation d'un nouveau paquet `linux-diting`, sinon le module de `updates/`
masquerait celui du paquet. Une fois la modification validée, la voie normale reste le paquet : en
faire un patch dans `pkgbuilds/linux-diting/`, puis
`ONLY=linux-diting scripts/build-in-container.sh scripts/20-construire-paquets.sh`.

Les autres pilotes du modem (`qcom_q6v5_pas` pour le remoteproc MPSS, `rmtfs_mem`, `qrtr`,
`qrtr-smd`) sont ceux de Linux 7.2.10, sans modification pour le modem. Pour les recompiler, même
méthode à partir de l'arbre de l'étape a, avec la cible du module :
`make LOCALVERSION= KBUILD_MODPOST_WARN=1 drivers/remoteproc/qcom_q6v5_pas.ko`.

## 11. Pistes ouvertes
- Rendre configurable l'abonnement du correctif `0002` (deux SIM actives).
- L'abonnement data désigné du modem n'est pas modifié (libqmi n'a pas de message pour cela). Le
  correctif contourne le problème client par client.
- Veille : sur batterie, les indications QMI du modem (état du réseau) réveillent le téléphone de
  temps en temps. ModemManager ne demande pas au modem de les couper écran éteint, contrairement à
  Android.
- Empaqueter ModemManager corrigé (PKGBUILD dans `pkgbuilds/`) au lieu de le recompiler à la main.
