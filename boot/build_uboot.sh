#!/usr/bin/env bash
set -euo pipefail
# Source is the AMD xilinx-v2025.1 release tree, copied before modifications.
here=$(cd "$(dirname "$0")" && pwd)
base=${1:-/home/developer/zu4ev/sc130-platform-uboot}
source_tree=${2:-/home/developer/zu4ev/bootloader-build/u-boot-xlnx-xilinx-v2025.1}
mkdir -p "$base"
if [ ! -d "$base/source" ]; then cp -a "$source_tree" "$base/source"; fi
src="$base/source"; out="$base/build"
cp "$here/zumini-sc130-qspi.dts" "$src/arch/arm/dts/"
export CROSS_COMPILE=aarch64-linux-gnu-
make -C "$src" O="$out" xilinx_zynqmp_virt_defconfig
"$src/scripts/config" --file "$out/.config" --set-str DEFAULT_DEVICE_TREE zumini-sc130-qspi --set-str OF_LIST zumini-sc130-qspi --set-val TEXT_BASE 0x04000000 --disable USE_PREBOOT --disable PREBOOT --set-val BOOTDELAY 0 --enable USE_BOOTCOMMAND --set-str BOOTCOMMAND 'setenv bootargs console=ttyPS0,115200 earlycon=cdns,mmio,0xff000000 rdinit=/init clk_ignore_unused; setenv fdt_high 0xffffffffffffffff; setenv initrd_high 0xffffffffffffffff; if sf probe 0:0 25000000 0; then if sf read 0x10000000 0x00c00000 0x01400000; then bootm 0x10000000#conf-sc130; fi; fi; echo QSPI Linux boot failed' --enable ENV_IS_NOWHERE --disable ENV_IS_IN_SPI_FLASH --disable ENV_IS_IN_MMC --disable ENV_IS_IN_FAT --disable ENV_IS_IN_NAND --disable ENV_IS_IN_EXT4 --disable ENV_IS_IN_UBI --enable FIT --enable FIT_FULL_CHECK --enable GZIP --enable CMD_SF --enable SPI_FLASH --enable SPI_FLASH_MICRON --enable ZYNQMP_GQSPI --disable BOOTSTD --disable BOOTSTD_DEFAULTS --disable DISTRO_DEFAULTS --disable DFU --disable DFU_TFTP --disable UPDATE_COMMON --disable UPDATE_TFTP --disable NETCONSOLE --enable NETDEVICES --disable NET --disable CMD_NET --disable USB --disable MMC --disable FPGA --disable VIDEO --disable EFI_LOADER
make -C "$src" O="$out" olddefconfig
make -C "$src" O="$out" -j4 u-boot u-boot.bin u-boot.elf
mkdir -p "$here/artifacts"
cp "$out/u-boot.elf" "$out/u-boot.bin" "$out/.config" "$here/artifacts/"
cp "$out/dts/dt.dtb" "$here/artifacts/u-boot.dtb"
(cd "$here/artifacts"; find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum) > "$here/artifacts/SHA256SUMS"





