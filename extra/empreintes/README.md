# Lecteur d'empreintes (étude de faisabilité)

Capteur Goodix optique sous l'écran. Rien ne passe par Linux : le bus SPI du
capteur appartient à la TrustZone, et une appli sécurisée Goodix fait la
capture, l'enregistrement et la comparaison. Android lui parle depuis son HAL
(`libgf_hal.so`, `fingerprint.goodix_fod.so`) par l'interface QSEECOM.

## Ce qu'on sait (2026-11-03)

| Point | Constat |
|---|---|
| DT Android (`fingerprint_goodix`) | reset GPIO 41, interruption GPIO 40, alimentations L1C et L3C ; aucun bus SPI côté Linux |
| Appli sécurisée | `gfdit.mdt` + `gfdit.b00`…`b08` dans la partition modem (`NON-HLOS.bin`, dossier `image/`), 15 Mo une fois assemblée |
| HAL Android | `vendor/lib64/libgf_hal.so` : `QSEECom_start_app`, `QSEECom_send_cmd`, classes `goodix::CaEntry`, `goodix::IonMemory` (zone carveout ION) |
| Interface TrustZone sous Linux | `qcomtee` actif, `/dev/tee0` ; l'ancienne `qseecom` est désactivée (« untested machine ») |
| Test [quic-teec](https://github.com/qualcomm/quic-teec) `-d` | OK : la TrustZone répond (service 143) |
| Chargement de `soter64` (85 Ko, appli de production) | **chargée** (contrôleur obtenu) ; `getAppObject` refusé : normal pour une appli QSEECOM |
| Chargement de `smplap64` (appli d'exemple) | refusé (`result 1`) |
| Chargement de `gfdit` | sans patch : impossible (plus de 4 Mo par tampon, `MAX_INBOUND_BUFFER_SIZE`, et mémoire partagée limitée à 4 Mo d'un bloc, `alloc_pages_exact`) ; **avec le patch 0001 (2026-11-04) : chargée** en mode 1, comme `soter64` |

Les applis se rassemblent en un `.mbn` avec `tools/mdt-squash.py` de
diting-ubuntu. Le test de quic-teec (`build/tests/qcomteetest -l DOSSIER 1 99`)
attend le fichier sous le nom `smcinvoke_skeleton_ta64.mbn` ; la commande 99,
inconnue, arrête le test juste après le chargement.

## Patch 0001 : grosses zones de mémoire partagée

Au-delà de 4 Mo, le pilote `qcomtee` prend la mémoire dans la réserve CMA
(`dma_alloc_pages`). Module à compiler sur le téléphone :

```sh
cd ~/linux-7.2.10
patch -Np1 < ~/alarm-xiaomi-diting/extra/empreintes/0001-tee-qcomtee-Allocate-large-shared-buffers-from-CMA.patch
make LOCALVERSION= KBUILD_MODPOST_WARN=1 drivers/tee/qcomtee/qcomtee.ko
sudo install -Dm644 -t /usr/lib/modules/$(uname -r)/updates/ drivers/tee/qcomtee/qcomtee.ko
sudo depmod -a
sudo reboot
```

## Suite

1. ~~Charger `gfdit`~~ : fait.
2. Envoyer une commande « à l'ancienne » (tampon requête/réponse) à une appli
   chargée par IAppLoader, comme le fait QSEECom sur les plateformes récentes.
3. Retrouver le protocole Goodix dans `libgf_hal.so`.
4. Allumer la zone du doigt (local HBM, commandes de la dalle) pendant la capture.

## Route QSEECOM (2026-11-04)

`gfdit` est une appli QSEECOM : elle répond à des commandes (tampon requête +
réponse), pas à des objets. Elle n'est donc pas joignable par `qcomtee`. Le
pilote QSEECOM de Dawid Wróbel et Sam Day ([samcday/linux, branche
`wip/sargo-fingerprint`](https://github.com/samcday/linux/pull/14)), rétroporté
dans le paquet `linux-diting` (patch `0918`, option `TEE_QSEECOM`), charge une
appli par son nom et lui transmet des commandes. Le patch CMA ci-dessus est
dans le paquet sous le n° `0919`.

Comme `QCOM_SCM` est intégré au noyau, il faut **reconstruire et installer le
paquet `linux-diting`** (pas seulement un module).

Le pilote cherche l'appli dans le dossier des firmwares : copier
`gfdit.mdt` et `gfdit.b00` à `gfdit.b08` (de `NON-HLOS.bin`, dossier
`image/`) dans `/usr/lib/firmware/`. Ces fichiers sont signés Xiaomi : ne pas
les publier.

`qseecom-cli.c` charge l'appli, ouvre une session et envoie des requêtes
brutes :

```sh
cc -O2 -Wall -o qseecom-cli qseecom-cli.c
sudo modprobe qseecomtee
sudo ./qseecom-cli gfdit
```
