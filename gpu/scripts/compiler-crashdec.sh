#!/bin/bash
# Compile crashdec (décodeur de devcoredump Adreno) depuis les sources de Mesa,
# sur un PC (pas sur le téléphone). Usage : compiler-crashdec.sh [version]
# Les sources Mesa sont prises sur l'archive Ubuntu (même version que l'hôte).
set -euo pipefail
v="${1:-26.2.6}"
mkdir -p crashdec-build && cd crashdec-build
curl -fLO "http://archive.ubuntu.com/ubuntu/pool/main/m/mesa/mesa_${v}.orig.tar.xz"
tar xf "mesa_${v}.orig.tar.xz"
cd "mesa-${v}"
# dépendances (Debian/Ubuntu) : meson ninja liblua5.4-dev libxml2-dev flex bison python3-mako python3-yaml
meson setup build -Dgallium-drivers= -Dvulkan-drivers= -Dglx=disabled -Degl=disabled \
  -Dgbm=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false -Dplatforms= \
  -Dllvm=disabled -Dtools=freedreno -Dvalgrind=disabled -Dlibunwind=disabled \
  -Dzstd=disabled -Dbuild-tests=false
ninja -C build src/freedreno/decode/crashdec
echo "→ $(pwd)/build/src/freedreno/decode/crashdec"
