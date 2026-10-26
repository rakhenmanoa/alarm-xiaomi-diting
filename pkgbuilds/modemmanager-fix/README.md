# ModemManager : correctif du plantage (netlink)

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
    makepkg -so
    curl -LO https://raw.githubusercontent.com/rakhenmanoa/alarm-xiaomi-diting/main/pkgbuilds/modemmanager-fix/0001-netlink-fix-use-after-free-calling-completion_fn-on-.patch
    patch -d src/ModemManager -p1 < 0001-netlink-fix-use-after-free-calling-completion_fn-on-.patch
    makepkg -e --nocheck
    ls *.pkg.tar.*
    sudo pacman -U <les paquets modemmanager et libmm-glib listés>

(adapter le dossier source si `ls src` montre un autre nom que
`ModemManager`). Une mise à jour d'Arch vers une version sans le correctif
le ferait revenir ; les versions publiées après le 4 octobre 2026 l'ont.
