# ISP matériel (ICP / BPS / IPE) — état des lieux

Retour au [README principal](../README.md) · [scoreboard](../scoreboard.md).

Aujourd'hui libcamera traite les images brutes sur le CPU (ISP logiciel). Le diting a
pourtant un vrai ISP matériel hors ligne : un cœur de contrôle **ICP** (Xtensa LX7) qui fait
tourner un firmware, et deux moteurs, **BPS** (Bayer) et **IPE** (post-traitement).
Cette page note ce qu'on sait, pour reprendre le travail quand un pilote sera utilisable.

## État upstream (16 nov. 2026)

- **ICP + BPS + IPE** : RFC « very loose » de Bryan O'Donoghue (Linaro, 3 juil. 2026, v1),
  pour le x1e80100 seulement. Elle démarre l'ICP et échange en HFI, mais ne traite aucune
  trame (l'envoi de trame du BPS est désactivé par un `#if 0`, la destruction d'un handle
  échoue). Rien de fusionné.
- **OPE** (autre moteur, puces d'entrée de gamme) : série de Loïc Poulain, v5 le
  24 juil. 2026, famille Agatti. Fonctionne, mais ne concerne pas le SM8475.

Il n'existe donc pas encore de pilote à adapter « tel quel » : ce qui suit prépare le terrain.

## Ce qu'on a vérifié sur le diting

Source : device tree Android (`vendor_boot_a.img`, bloc « Cape SoC »), lu avec
`tools/dt-inventaire.py`, et le firmware du `vendor` de la sauvegarde HyperOS.

| Élément | diting (SM8475) | RFC (x1e80100) |
|---|---|---|
| Cœur ICP | `qcom,cam-lx7`, 1 BPS, 1 IPE | ICP |
| Registres ICP | `lx7_csr` 0xac01000 (0x400), `lx7_cirq` 0xac01800 (0x400), `lx7_wd0` 0xac04000 (0x1000) | ICP à 0xac01000 |
| BPS | `qcom,cam-bps680` à 0xac2c000 (0x7800) | BPS à 0xac2c000 |
| IPE | `qcom,cam-ipe680` à 0xac42000 (0x16000) | non détaillé |
| Interruption ICP | SPI 463, nom `lx7` | IRQ index 0 |
| CPAS | 0xac13000, caméra NoC 0xac19000, RPMh 0xbbf0000, SPI 459 | — |
| Interconnects | `cam_ahb`, `cam_hf_0`, `cam_sf_0`, `cam_sf_icp` | `ahb`, `hf_0`, `sf_0`, `sf_icp` |
| Firmware | `CAMERA_ICP` (`.mdt` + segments `.b00`…`.b21`), chargé par PIL (`icp_use_pil`) | `CAMERA_ICP.mdt`, chargé via `qcom_scm_pas_auth_and_reset` |
| Taille du firmware | 0x700000 (7 Mo, segments du `.elf` et région IOMMU `fw_uncached`) | 7 340 032 octets |
| Zone réservée du firmware | `camera_region@9f500000`, 0x800000 (8 Mo), `no-map` : le firmware (7 Mo) y tient | zone de 8 Mo à 0x8e100000 |

Les adresses de l'ICP et du BPS, les noms d'interconnects et la taille du firmware sont
identiques. Les horloges ont les mêmes fonctions avec des noms légèrement différents
(BPS : `bps_ahb`, `bps_fast_ahb`, `bps`, `cpas_bps` ; IPE : `ipe_nps_ahb`,
`ipe_nps_fast_ahb`, `ipe_pps_fast_ahb`, `ipe_nps`, `ipe_pps`, `cpas_ipe_nps`).

Flux IOMMU de l'ICP (SID, masque 0x420 ou 0x400) : `0x2000 0x2020 0x2040 0x2060 0x2080
0x2100 0x2120 0x2400 0x2420 0x2440 0x2460 0x2480 0x2500 0x2520`.

## Ce qui reste inconnu

1. **`pas-id`** de l'ICP pour le SM8475 : absent du nœud Android. Le x1e80100 utilise 33.
   Une valeur fausse fait refuser le démarrage par TrustZone.
2. **Mémoire HFI** : la RFC demande une deuxième `memory-region` (pool `shared-dma-pool`
   d'environ 272 Mo). Android n'en déclare pas de dédiée, il alloue dynamiquement. Il faudra
   en créer une dans le DTS, et vérifier que `camera_region@9f500000` n'est pas déjà
   déclarée autrement dans `sm8450.dtsi`.
3. **Compatibilité du HFI** : même taille et même chargeur ne prouvent pas que le firmware du
   SM8475 parle le même protocole que celui du x1e80100. Seul un démarrage réel le dira.
4. **Numéros d'horloges** (`camcc`, `gcc`) du DT Android à rapprocher des identifiants du
   `camcc` du noyau, et GDSC `titan_top`, `bps`, `ipe_0` correspondants.

## Prochaines étapes

1. Trouver le `pas-id` de l'ICP et vérifier la zone `camera_region` dans le DTS du dépôt.
2. Écrire les nœuds `icp`, `bps`, `ipe` dans le DTS du diting (désactivés par défaut).
3. Reprendre la RFC sur le noyau du dépôt, avec les valeurs du diting, et tenter un
   démarrage de l'ICP (échange HFI) sans traiter d'image.
4. Seulement après : traitement d'une trame par le BPS, puis intégration libcamera.

Les fichiers du firmware (`CAMERA_ICP.*`) sont propriétaires : ils ne sont pas dans le dépôt.
