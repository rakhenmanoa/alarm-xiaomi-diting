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
