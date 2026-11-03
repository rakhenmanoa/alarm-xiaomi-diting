# Limite de charge (expérimental)

Le pilote `qcom_battmgr` de Linux sait arrêter la charge à un seuil
(`charge_control_end_threshold`) et la reprendre plus bas
(`charge_control_start_threshold`), mais seulement pour les firmwares SM8550 et
plus récents (commande `BATTMGR_CHG_CTRL_LIMIT_EN`, `0x48`). Le diting est traité
comme un SM8350 : les seuils n'existent pas.

Le patch `0001` ajoute un paramètre de module, **désactivé par défaut**,
`sm8350_charge_control`, qui expose ces seuils sur le diting pour savoir si son
firmware de charge (ADSP) connaît la commande. Il s'applique après les patches
`0025`, `0091` et `0093` de diting-ubuntu (déjà dans l'arbre du paquet).

## Tester sur le téléphone

Avec l'arbre noyau de `docs/modules-sur-le-telephone.md` :

```sh
cd ~/linux-7.2.10
patch -Np1 < ~/alarm-xiaomi-diting/extra/limite-charge/0001-power-supply-qcom_battmgr-Optional-charge-control-on.patch
make LOCALVERSION= KBUILD_MODPOST_WARN=1 drivers/power/supply/qcom_battmgr.ko
sudo install -Dm644 -t /usr/lib/modules/$(uname -r)/updates/ drivers/power/supply/qcom_battmgr.ko
sudo depmod -a
echo "options qcom_battmgr sm8350_charge_control=1" | sudo tee /etc/modprobe.d/diting-limite-charge.conf
sudo reboot
```

Puis, chargeur branché :

```sh
ls /sys/class/power_supply/qcom-battmgr-bat/charge_control_*
echo 80 | sudo tee /sys/class/power_supply/qcom-battmgr-bat/charge_control_end_threshold
```

- `Connection timed out` (après une seconde) : le firmware ne connaît pas la
  commande, la piste s'arrête là.
- Pas d'erreur : vérifier que la charge s'arrête vraiment à 80 %
  (`cat /sys/class/power_supply/qcom-battmgr-bat/status`, `capacity`).

Retour en arrière : supprimer `/etc/modprobe.d/diting-limite-charge.conf` et le
module de `updates/`, puis `sudo depmod -a`.

## Résultat du patch 0001 (2026-11-03)

Le firmware **accepte** la commande (pas de délai dépassé) mais **ne
l'applique pas** : seuil d'arrêt à 60 % écrit à 83 %, le téléphone continue de
charger à environ 2 A. Piste abandonnée.

## Patch 0002 : accès brut aux propriétés de la batterie

Le patch `0002` (à appliquer après `0001`) ajoute
`/sys/kernel/debug/qcom_battmgr/prop` : on y écrit `get N` ou `set N V`, avec
`N` l'indice de propriété **du firmware** (sans décalage), puis on lit la
réponse (`prop=… value=… result=…`). But : savoir si la propriété « charge
activée » (`BATT_CHG_CTRL_EN`, indice 24 dans Linux, **25** dans le firmware
Xiaomi qui a `BATT_CONSTANT_CURRENT` en 12) coupe la charge.

```sh
cd ~/linux-7.2.10
patch -Np1 < ~/alarm-xiaomi-diting/extra/limite-charge/0002-power-supply-qcom_battmgr-debugfs-access-to-raw-batt.patch
make LOCALVERSION= KBUILD_MODPOST_WARN=1 drivers/power/supply/qcom_battmgr.ko
sudo install -Dm644 -t /usr/lib/modules/$(uname -r)/updates/ drivers/power/supply/qcom_battmgr.ko
sudo reboot
```

Puis, en root (`sudo -i`), chargeur branché :

```sh
P=/sys/kernel/debug/qcom_battmgr/prop
echo "get 4" > $P; cat $P     # contrôle : niveau de charge × 100
echo "get 25" > $P; cat $P    # charge activée (1 attendu)
echo "set 25 0" > $P; cat $P  # couper la charge
cat /sys/class/power_supply/qcom-battmgr-bat/status
echo "set 25 1" > $P          # la rétablir
```
