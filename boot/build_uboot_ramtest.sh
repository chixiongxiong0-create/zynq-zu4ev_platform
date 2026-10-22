#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
base=${1:-/home/developer/zu4ev/sc130-platform-uboot}
src="$base/source"; out="$base/ramtest-build"
mkdir -p "$out" "$here/ramtest-artifacts"
cp "$here/artifacts/.config" "$out/.config"
"$src/scripts/config" --file "$out/.config" --set-str BOOTCOMMAND 'setenv bootargs console=ttyPS0,115200 earlycon=cdns,mmio,0xff000000 rdinit=/init clk_ignore_unused; setenv fdt_high 0xffffffffffffffff; setenv initrd_high 0xffffffffffffffff; bootm 0x10000000#conf-sc130; echo RAM Linux boot failed'
export CROSS_COMPILE=aarch64-linux-gnu-
make -C "$src" O="$out" olddefconfig
make -C "$src" O="$out" -j4 u-boot u-boot.bin u-boot.elf
cp "$out/u-boot.elf" "$out/u-boot.bin" "$out/.config" "$here/ramtest-artifacts/"
cp "$out/dts/dt.dtb" "$here/ramtest-artifacts/u-boot.dtb"
(cd "$here/ramtest-artifacts"; sha256sum .config u-boot.elf u-boot.bin u-boot.dtb) > "$here/ramtest-artifacts/SHA256SUMS"
