#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
base=${1:-/home/developer/zu4ev/sc130-platform-atf}
source_tree=${2:-/home/developer/zu4ev/bootloader-build/arm-trusted-firmware-xilinx-v2025.1}
mkdir -p "$base"
if [ ! -d "$base/source" ]; then cp -a "$source_tree" "$base/source"; fi
# Release build uses standard OCM range; no PRELOADED_BL33_BASE override.
make -C "$base/source" BUILD_BASE="$base/build" CROSS_COMPILE=aarch64-linux-gnu- -j4 PLAT=zynqmp RESET_TO_BL31=1 DEBUG=0 ZYNQMP_CONSOLE=cadence0 bl31
mkdir -p "$here/artifacts"
cp "$base/build/zynqmp/release/bl31/bl31.elf" "$here/artifacts/bl31.elf"
aarch64-linux-gnu-readelf -h -l "$here/artifacts/bl31.elf" > "$here/../reports/boot/bl31-elf.txt"
(cd "$here/artifacts"; find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum) > "$here/artifacts/SHA256SUMS"

