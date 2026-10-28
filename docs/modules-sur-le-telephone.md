# Compiler des modules noyau et le device tree sur le téléphone

Pour tester un pilote ou une modification du device tree sans reconstruire le
paquet `linux-diting` complet (15 à 60 minutes sur le PC) : on prépare une fois
un arbre noyau identique à celui du paquet sur le téléphone, puis on compile
seulement les modules et le DTB voulus, en quelques secondes.

Ça marche parce que le noyau du diting n'impose ni signature des modules
(`MODULE_SIG`) ni versionnage des symboles (`MODVERSIONS`) : un module compilé à
part se charge dès que la version du noyau (`vermagic`) correspond exactement.

Exemple utilisé ici : les pilotes des caméras frontale (`imx596`) et macro
(`gc02m1`), patches `0905` et `0906` de `pkgbuilds/linux-diting`.

## 1. Préparer l'arbre noyau (une seule fois)

Outils nécessaires :

```sh
sudo pacman -S --needed base-devel bc git
```

Sources du noyau, à la version du paquet (`_kver` dans `pkgbuilds/linux-diting/PKGBUILD`) :

```sh
cd ~
git clone --depth 1 --branch v7.2.10 https://github.com/gregkh/linux linux-7.2.10
```

Patches de diting-ubuntu, au commit du paquet (`_ditingcommit`) :

```sh
git clone https://github.com/royka1/diting-ubuntu ~/diting-ubuntu
git -C ~/diting-ubuntu checkout c68ff47db94792fb5fd608e98d1ae489c2e592b1
```

Appliquer tous les patches (ceux de diting-ubuntu, puis les `09xx` de ce dépôt,
à jour avec `git pull`), dans l'ordre du `prepare()` du PKGBUILD :

```sh
cd ~/linux-7.2.10
for p in ~/diting-ubuntu/kernel/patches/*.patch ~/alarm-xiaomi-diting/pkgbuilds/linux-diting/09*.patch; do
	patch -Np1 -s < "$p" || echo "ÉCHEC $p"
done
```

Aucune ligne `ÉCHEC` ne doit s'afficher.

Configuration du noyau installé (le paquet l'installe), plus les nouveaux pilotes :

```sh
cp /usr/share/doc/linux-diting/config .config
scripts/config --module VIDEO_IMX596 --module VIDEO_GC02M1
make LOCALVERSION= olddefconfig
make LOCALVERSION= -j8 modules_prepare
```

`LOCALVERSION=` (vide) est indispensable, comme dans le PKGBUILD : sans lui,
l'arbre git ajoute un « + » à la version et les modules ne se chargent pas.

Vérifier que les deux lignes sont identiques :

```sh
make -s LOCALVERSION= kernelrelease; uname -r
```

## 2. Compiler les modules et le device tree

```sh
make LOCALVERSION= KBUILD_MODPOST_WARN=1 drivers/media/i2c/imx596.ko drivers/media/i2c/gc02m1.ko
make LOCALVERSION= qcom/sm8475-xiaomi-diting.dtb
```

Les avertissements `WARNING: modpost: "…" undefined!` sont normaux : l'arbre ne
connaît pas les symboles du noyau installé (pas de `Module.symvers`), ils sont
résolus au chargement. `depmod` calcule les dépendances lui-même.

Pour un autre pilote, remplacer les chemins des `.ko`.

## 3. Installer

Modules, dans `updates/` (prioritaire sur les modules du paquet) :

```sh
sudo install -Dm644 -t /usr/lib/modules/$(uname -r)/updates/ drivers/media/i2c/imx596.ko drivers/media/i2c/gc02m1.ko
sudo depmod -a
```

Device tree, en gardant l'ancien :

```sh
sudo cp /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb.orig
sudo install -m644 arch/arm64/boot/dts/qcom/sm8475-xiaomi-diting.dtb /boot/dtbs/qcom/
```

Firmware éventuel. Pour les caméras, les tables de registres se génèrent sur le
PC, depuis `device-data` (elles contiennent les réglages du vendeur : ne pas les
publier) :

```sh
# sur le PC
python3 -I tools/camera/chromatix-to-camregs.py device-data/camera/com.qti.sensormodule.diting_ofilm_gc02m1_macro.bin /tmp/gc02m1-macro.camregs
python3 -I tools/camera/chromatix-to-camregs.py device-data/camera/com.qti.sensormodule.diting_sunny_imx596_front.bin /tmp/imx596-front.camregs
scp /tmp/imx596-front.camregs /tmp/gc02m1-macro.camregs alarm@172.16.42.1:
```

```sh
# sur le téléphone
sudo install -m644 -t /usr/lib/firmware/qcom/sm8475/xiaomi/diting/ ~/imx596-front.camregs ~/gc02m1-macro.camregs
```

Le DTB est dans `vendor_boot` : il faut régénérer et écrire les images de
démarrage (le hook mkinitcpio les reconstruit avec `/boot/dtbs/…`) :

```sh
sudo mkinitcpio -P
sudo diting-install-bootimg
sudo reboot
```

Après le redémarrage :

```sh
sudo dmesg | grep -i -E "imx596|gc02m1|cci"
```

Pour une simple modification de module (sans toucher au DTB), il suffit de
recompiler le `.ko`, de le réinstaller, puis `sudo depmod -a` et
`sudo modprobe -r imx596; sudo modprobe imx596` (ou un redémarrage).

## 4. Revenir en arrière, ou passer au paquet

- Retirer les modules de test : `sudo rm /usr/lib/modules/$(uname -r)/updates/{imx596,gc02m1}.ko`
  puis `sudo depmod -a`. **À faire aussi après l'installation d'un paquet
  `linux-diting` qui contient ces pilotes**, sinon les modules de `updates/`
  masqueraient ceux du paquet.
- Remettre l'ancien device tree : `sudo mv /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb.orig /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb`,
  puis `sudo mkinitcpio -P` et `sudo diting-install-bootimg`. Le paquet
  `linux-diting` réinstalle de toute façon son propre DTB à chaque mise à jour.
- Une fois le pilote validé, la voie normale reste le paquet : patch dans
  `pkgbuilds/linux-diting`, puis
  `ONLY="linux-diting" scripts/build-in-container.sh scripts/20-construire-paquets.sh`.
