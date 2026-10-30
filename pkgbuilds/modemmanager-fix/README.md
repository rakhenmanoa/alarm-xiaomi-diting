# ModemManager : correctifs pour le diting

## 1. Plantage (netlink)

ModemManager 1.24.2 d'Arch plante (SIGSEGV) toutes les 20 à 30 s sur le
diting en recréant la liaison de données : `transaction_complete()` lit
`tr->completion_fn` après avoir libéré la transaction. La 4G coupe à chaque
fois. Le correctif est le commit officiel 9317cbc4 (Dan Williams,
2026-10-04), le même que diting-ubuntu ; il s'applique tel quel à 1.24.2.

Recompilation sur le téléphone, à partir du PKGBUILD d'Arch, une commande à
la fois :

    git clone https://gitlab.archlinux.org/archlinux/packaging/packages/modemmanager.git ~/modemmanager
    cd ~/modemmanager
    git checkout 1.24.2-2
    makepkg -soA
    curl -LO https://raw.githubusercontent.com/rakhenmanoa/alarm-xiaomi-diting/main/pkgbuilds/modemmanager-fix/0001-netlink-fix-use-after-free-calling-completion_fn-on-.patch
    patch -d src/ModemManager -p1 < 0001-netlink-fix-use-after-free-calling-completion_fn-on-.patch
    curl -LO https://raw.githubusercontent.com/rakhenmanoa/alarm-xiaomi-diting/main/pkgbuilds/modemmanager-fix/0002-bearer-qmi-bind-WDS-client-to-primary-subscription.patch
    patch -d src/ModemManager -p1 < 0002-bearer-qmi-bind-WDS-client-to-primary-subscription.patch
    makepkg -eA --nocheck
    ls *.pkg.tar.*
    sudo pacman -U <les paquets modemmanager et libmm-glib listés>

(adapter le dossier source si `ls src` montre un autre nom que
`ModemManager`). Une mise à jour d'Arch vers une version sans le correctif
le ferait revenir ; les versions publiées après le 4 octobre 2026 l'ont.

Pour que `pacman -Syu` ne remplace pas ces paquets par ceux d'Arch, dans
`/etc/pacman.conf`, section `[options]` :

    IgnorePkg = modemmanager libmm-glib

## 2. Données mobiles : `cm error: no-service`

Le modem s'enregistre en 4G (`registered`, `packet service state:
attached`) mais toute session data échoue aussitôt : `Start Network` →
`CallFailed`, raison détaillée `(3,2001)` `[cm] no-service`, quels que soient
l'APN, le type d'IP ou le profil.

Cause : le modem est multi-SIM. ModemManager ne lie jamais son client WDS à
un abonnement, il reste donc à `any`, et le modem envoie la session vers
l'abonnement data laissé par Android, qui n'est pas celui où la SIM est
provisionnée sous Linux (ici une eSIM, emplacement 2, `Primary GW: slot '2'`
dans `qmicli -d qrtr://0 --uim-get-card-status`).

Le patch `0002` lie le client WDS à l'abonnement primaire juste après la
liaison au canal data (IPv4 et IPv6). Un refus du modem n'est pas bloquant :
ModemManager note un avertissement et continue.

Diagnostic sans recompiler : `extra/qmi-dds-test/qmi-dds-test.py`, en Python
avec libqmi par GObject introspection. Il fait tout dans un seul processus :
en QRTR, le client WDS ne survit pas entre deux appels de qmicli, et qmicli
1.38 n'a pas d'option pour Bind Subscription.

    sudo pacman -S python-gobject   # si absent

    sudo python3 qmi-dds-test.py lire            # abonnement du client (any)
    sudo python3 qmi-dds-test.py test primary    # SESSION DATA OUVERTE

Ce choix vaut pour une SIM sur l'abonnement primaire, ce qui est le cas avec
une seule SIM active, physique ou eSIM. Avec deux SIM actives, il faudrait
rendre l'abonnement configurable. Le réglage persistant du modem (abonnement
data désigné, laissé par Android) n'est pas modifié : libqmi n'a pas de
message pour cela, le patch contourne le problème client par client.

Après installation :

    sudo systemctl restart ModemManager
    sudo nmcli connection modify Airtel gsm.apn internet connection.autoconnect yes
    sudo nmcli connection up Airtel
    nmcli device | grep -i gsm             # qrtr0 … connecté
    ping -c3 -I qmapmux0.0 1.1.1.1
