# Waydroid : stockage jamais monté, « espace insuffisant » dans le Play Store

Retour au [README principal](../README.md) · voir aussi [pieges.md](pieges.md).

Constaté sur diting avec Waydroid (image LineageOS 20 / Android 13, `lineage_waydroid_arm64_only`,
build 20260927), après une réinitialisation des images.

## Symptômes

- Play Store : les mises à jour et téléchargements échouent avec « espace insuffisant », alors que
  `df -h` montre plus de 180 Go libres (hôte et `/data` d'Android).
- Dans le shell Android, `/storage/emulated/0` est vide ou répond « Transport endpoint is not
  connected » ; `df` y montre un `tmpfs` de 3,1 Go au lieu du stockage partagé.
- `logcat` : boucle `vold` / `StorageManagerService` avec un délai de 20 s au démarrage de la
  session du stockage (`ExternalStorageServiceImpl`), puis réinitialisation et nouvel essai.
- Effets en cascade : Google Play Billing ne répond pas (délai dépassé dans `InitializeInAppService`),
  donc l'onglet « morceaux » de GuitarTuna reste sur « chargement » ; l'ajout d'un compte Google
  échoue après 90 s ; Play services reste en version ancienne.

Pistes écartées (ne changent rien) : réseau, MTU, IPv6, WebView, date, droits de `/data/media`,
noyau (FUSE présent), `persist.sys.fuse.passthrough.enable`, version du module Media Storage,
`pm clear` des fournisseurs de médias.

## Cause

Le manifeste VINTF du vendor (`/vendor/etc/vintf/manifest.xml`) déclare le HAL
`android.hardware.media.omx@1.0` (`IOmx` et `IOmxStore`, instance `default`), mais l'image ne
contient aucun service qui l'implémente (ni exécutable, ni `.rc`). Seul le service logiciel
`android.hardware.media.c2@1.x::IComponentStore/software` (`media.swcodec`) existe.

Quand le module du fournisseur de médias démarre, `TranscodeHelperImpl` crée un décodeur
(`MediaCodec.createDecoderByType`). `mediaserver` construit alors la liste des codecs
(`MediaCodecList::getLocalInstance` → `OmxInfoBuilder::buildMediaCodecList`) et appelle
`getService<IOmxStore>`. Comme le service est déclaré, libhidl attend son enregistrement sans
limite de temps (`Waiter::wait`). Tous les autres threads de `mediaserver` attendent ce même verrou :

- `media.player` n'est jamais enregistré (il manque dans `service list`) ;
- le module du fournisseur de médias reste bloqué dans `MediaProvider.onCreate` ;
- le montage FUSE de `/storage/emulated` ne se fait jamais.

## Diagnostic

Dans le shell Android (`sudo waydroid shell`) :

```
service list | grep -iE "media\.(player|codec|extractor|metrics)"   # media.player absent
ps -A | grep -iE "mediaserver|media\.swcodec"                       # mediaserver en futex_do_wait
lshal | grep -iE "media|omx|c2"                                     # pas d'IOmx ni d'IOmxStore
grep -n -B3 -A8 "media.omx" /vendor/etc/vintf/manifest.xml          # HAL pourtant déclaré
debuggerd -b $(pidof mediaserver)                                   # pile : getServiceInternal<IOmxStore>
```

`kill -3` sur le processus `com.android.providers.media.module` (fichier dans `/data/anr`, donc
`~/.local/share/waydroid/data/anr/` sur l'hôte) montre l'autre bout : son thread principal est
bloqué dans `BpMediaPlayerService::getCodecList`.

## Correction

On retire la déclaration du HAL OMX avec un overlay Waydroid, sans modifier les images.

```
sudo mkdir -p /var/lib/waydroid/overlay/vendor/etc/vintf
sudo cp /var/lib/waydroid/rootfs/vendor/etc/vintf/manifest.xml \
        /var/lib/waydroid/overlay/vendor/etc/vintf/manifest.xml
```

Éditer la copie et supprimer tout le bloc `<hal format="hidl">` dont le `<name>` est
`android.hardware.media.omx` (15 lignes, de `<hal format="hidl">` à `</hal>`, entre `ILight` et
`memtrack` dans notre image). Par exemple, après avoir repéré les lignes avec `grep -n`/`sed -n` :

```
sudo sed -i '96,110d' /var/lib/waydroid/overlay/vendor/etc/vintf/manifest.xml
grep -c "media.omx" /var/lib/waydroid/overlay/vendor/etc/vintf/manifest.xml   # doit afficher 0
```

Les numéros de lignes dépendent de l'image : les vérifier avant de supprimer.

Redémarrer Waydroid :

```
waydroid session stop; sudo systemctl restart waydroid-container; waydroid session start
```

## Vérification

Dans le shell Android :

```
service list | grep -ci "media.player"    # 1
ls /storage/emulated/0                    # Alarms, Android, DCIM, Download, ...
```

Les mises à jour du Play Store fonctionnent ensuite.

## À savoir

- L'overlay survit aux redémarrages mais une image du vendor changée plus tard peut avoir un
  manifeste différent : refaire la copie si le problème revient.
- Si `waydroid init -f` est relancé avec une autre image, vérifier que la déclaration OMX ne
  réapparaît pas dans le manifeste d'origine (l'overlay la masque tant qu'il est en place).
- Cette fiche ne répare pas la certification Play (voir la suite du suivi Waydroid : Play services
  à jour, compte Google, identifiant `android_id` à enregistrer sur
  `google.com/android/uncertified`).
