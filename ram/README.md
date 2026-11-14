# RAM du Xiaomi 12T Pro (diting) — mémoire réservée par le noyau

Ce dossier documente la mémoire que le device tree retire à Linux (`reserved-memory`), pourquoi elle ne
peut pas aller dans le zram, et le patch qui rend **512 Mio** inutilement réservés.

> **Ce sont des copies de référence, figées le 2026-11-14.** Les fichiers réellement construits restent
> ceux de `pkgbuilds/linux-diting/` : mettez d'abord ces originaux à jour, puis recopiez ici.

## Contenu

| Fichier | Contenu | Original |
|---|---|---|
| `patches/0930-arm64-dts-qcom-sm8475-xiaomi-diting-Drop-the-MTE-carveout.patch` | `/delete-node/ &mte_mem;` dans le DTS du diting | `pkgbuilds/linux-diting/0930-…` |

Le patch s'applique sur `arch/arm64/boot/dts/qcom/sm8475-xiaomi-diting.dts`, **fichier qui vient de la
série de patches de royka1/diting-ubuntu**. Il passe donc après cette série dans le `PKGBUILD` de
`linux-diting` (paquet `pkgrel=37`).

## 1. Le constat

Sur 8 Go physiques (`8141824K`), le noyau n'en voyait que 6,25 Gio :

```sh
grep MemTotal /proc/meminfo                    # 6557532 kB avant le patch
dmesg | grep "Memory:"                         # ... 1601116K reserved, 45056K cma-reserved
dmesg | grep -i "reserved mem"                 # la liste des zones
```

Environ **1,53 Gio** étaient retirés par des nœuds `no-map` du device tree.

## 2. Pourquoi pas dans le zram ?

Les zones `reserved-memory` sont des plages d'adresses **physiques fixes** utilisées directement (DMA) par
les coprocesseurs et le firmware : modem, DSP, TrustZone, hyperviseur Gunyah… Le noyau ne les gère pas :
il ne peut ni les compresser, ni les déplacer, ni les swapper. Le zram ne compresse que les pages que le
noyau gère lui-même.

La seule façon de récupérer cette RAM est donc de **supprimer la réservation** quand personne ne l'utilise.

Le CMA, lui, n'est pas perdu (le noyau y place des pages déplaçables) et il n'est que de 44 Mio ici :
rien à gagner de ce côté.

## 3. Les zones du diting

Étiquettes lues dans `sm8450.dtsi` et `sm8475-xiaomi-diting.dts`, tailles d'après `dmesg` :

| Adresse | Étiquette | Taille | Rôle | Verdict |
|---|---|---|---|---|
| `0xc0000000` | `mte_mem` | 512 Mio | Stockage des tags MTE de la plateforme de référence | **Supprimée (patch 0930)** |
| `0x8bc00000` | `mpss_mem` | 306 Mio | Firmware du modem | Supprimable si pas de SIM (§ 6) |
| `0xbb000000` | `oem_vm_mem` | 80 Mio | VM de l'hyperviseur Gunyah | À garder |
| `0xe0b00000` | `trust_ui_vm_mem` | 75 Mio | VM sécurisée (Trusted UI) | À garder |
| `0x80c00000` | `cdsp_secure_heap` | 70 Mio | Tas sécurisé du cDSP | À garder |
| `0xed900000` | `trusted_apps_ext_mem` | 59 Mio | Applications TrustZone | À garder |
| `0xea000000` | `trusted_apps_mem` | 57 Mio | Applications TrustZone | À garder |
| `0x9fd00000` | `adsp_mem` | 49 Mio | DSP audio (son) | À garder |
| `0xb8000000` | `splash_region` | 43 Mio | Écran de démarrage | Réécrite par l'ABL (§ 7) |
| `0x89900000` | `cdsp_mem` | 32 Mio | DSP de calcul | À garder |
| `0x88000000` | `slpi_mem` | 25 Mio | DSP des capteurs | À garder |
| `0x80000000`… | hyperviseur, XBL, AOP, smem… | quelques Mio | Chaîne de démarrage | **Ne jamais toucher** |

Les zones des VM et de TrustZone appartiennent à l'hyperviseur : les rendre à Linux fait planter le
téléphone (« synchronous external abort ») dès que le noyau écrit dedans.

## 4. La zone MTE (512 Mio)

`mte_mem` (`0xc0000000`, `0x20000000`) est mise de côté pour les tags de la *Memory Tagging Extension*.
Sa taille correspond à 1/32 de 16 Gio (le maximum du SoC), pas aux 8 Go du diting. Dans le DT vivant, le
nœud n'a **ni `phandle` ni utilisateur `memory-region`** :

```sh
dtc -I fs -O dts /sys/firmware/devicetree/base -o ~/live.dts 2>/dev/null
grep -n -A4 "memory@c0000000" ~/live.dts
```

Restait à savoir si le firmware la protège (XPU). Test fait le 2026-11-14 : sans le nœud, le téléphone
démarre, gagne les 512 Mio et tient `stress-ng` sur 90 % de la RAM pendant deux minutes. La zone est donc
libre sur cet appareil, d'où le patch `0930`.

## 5. Tester une suppression sans reconstruire le noyau

Le DTB est dans **`vendor_boot.img`** (boot v4), pas dans `boot.img` : `fastboot boot` ne permet pas de
tester un DTB. On modifie le DTB installé et on garde une sauvegarde de la partition sur le PC.

Sauvegarde (copier ensuite les fichiers **sur le PC**) :

```sh
slot=$(sudo /usr/lib/diting/diting-mark-boot-successful --print-slot); echo $slot
sudo dd if=/dev/disk/by-partlabel/vendor_boot_$slot of=~/vendor_boot-avant-ram.img bs=4M
sudo dd if=/dev/disk/by-partlabel/boot_$slot of=~/boot-avant-ram.img bs=4M
sudo cp /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb.avant-ram
```

Suppression d'un nœud (`fdtput -r`), par exemple la zone MTE :

```sh
fdtget -l /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /reserved-memory | grep c0000000
sudo fdtput -r /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /reserved-memory/memory@c0000000
```

Puis :

```sh
sudo mkinitcpio -P
sudo diting-install-bootimg
sudo reboot
```

Vérification :

```sh
grep MemTotal /proc/meminfo
stress-ng --vm 4 --vm-bytes 90% --timeout 120s
```

Le `stress-ng` est indispensable : le noyau n'écrit dans la zone rendue qu'une fois la RAM bien remplie.
Un démarrage réussi ne prouve rien.

### Retour arrière

Si le téléphone ne démarre plus ou redémarre pendant le test, en fastboot (power + volume bas), depuis le
PC (remplacer `a` par le slot) :

```sh
fastboot flash vendor_boot_a vendor_boot-avant-ram.img
fastboot reboot
```

Puis, sur le téléphone, lire la cause (le ramoops du DTS survit à un redémarrage à chaud) et remettre le
DTB :

```sh
cat /sys/fs/pstore/console-ramoops*
sudo cp /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb.avant-ram /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb
sudo mkinitcpio -P
```

Les modifications `fdtput` disparaissent à la prochaine mise à jour de `linux-diting` : une suppression
validée doit devenir un patch `09xx`.

## 6. Piste non retenue : le modem (306 Mio)

À réserver à un usage sans SIM ni données mobiles. Test avec `fdtput` :

```sh
sudo fdtput -t s /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /soc@0/remoteproc@4080000 status disabled
sudo fdtput -t s /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /soc@0/ipa@3f40000 status disabled
sudo fdtput -r /boot/dtbs/qcom/sm8475-xiaomi-diting.dtb /reserved-memory/memory@8bc00000
```

Équivalent DTS :

```dts
&remoteproc_mpss {
	status = "disabled";
};

&ipa {
	status = "disabled";
};

/delete-node/ &mpss_mem;
/delete-node/ &rmtfs_mem;
```

## 7. Piste non retenue : le splash (43 Mio)

Le DTS déclare `splash_mem: memory@b8000000`, mais le DT vivant montre un nœud `splash_region` à la même
adresse : l'**ABL réécrit lui-même ce nœud** au démarrage. Le supprimer du DTB ne servirait probablement à
rien. De plus, garder le `simple-framebuffer` de `/chosen` sans sa réservation ferait écrire la console
dans de la RAM donnée à d'autres.

## 8. Le zram

Le zram reste utile pour la RAM que Linux gère (`zram-size = ram / 2`, fichier
`pkgbuilds/diting-device/zram-generator.conf`). Avec le patch `0930`, `ram` vaut 512 Mio de plus, donc le
zram grandit aussi.
