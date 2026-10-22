#!/bin/bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BASE=${SC130_BUILD_ROOT:-/home/developer/zu4ev/sc130-platform-build}
BASE=$(realpath -m "$BASE")
[[ "$BASE" == /home/developer/zu4ev/sc130-platform-build ]] || { echo 'Dedicated platform build root required';exit 2; }
OLD=/home/developer/zu4ev/sc130-uvc-v2-build
SRC=$BASE/linux-src; KOUT=$BASE/kernel; BUNDLE=$BASE/bundle
mkdir -p "$BASE" "$BUNDLE" "$BASE/module" "$HERE/dist" "$HERE/reports"
phase=${1:-all}
if [[ $phase == userspace || $phase == all ]]; then
 make -C "$HERE/../software" OUT="$BASE/native-software" CC=cc test
 make -C "$HERE/../software" OUT="$BASE/software" CC=aarch64-linux-gnu-gcc LDFLAGS=-static all
 export SC130_BROKER="$BASE/software/sc130_platform_bridge"
fi
if [[ $phase == test || $phase == all ]]; then
 cc -O2 -Wall -Wextra -Werror "$HERE/tests/test_dual.c" -o "$BASE/test_dual"
 "$BASE/test_dual"
 cc -O2 -Wall -Wextra -Werror "$HERE/tests/test_stage_frame.c" -o "$BASE/test_stage_frame"
 cc -O2 -Wall -Wextra -Werror "$HERE/stage-frame.c" -o "$BASE/stage-frame-native"
 python3 "$HERE/tests/test_stage_cli.py" "$BASE/stage-frame-native"
 "$BASE/test_stage_frame"
 python3 "$HERE/tests/test_package.py"
 for script in "$HERE/"*.sh; do bash -n "$script"; done
fi
if [[ $phase == kernel || $phase == all ]]; then
 [[ -d "$SRC" ]] || cp -a --reflink=auto "$OLD/linux-src" "$SRC"
 [[ -d "$KOUT" ]] || cp -a --reflink=auto "$OLD/kernel" "$KOUT"
 for change in "$HERE/patches/"*.patch; do
  if patch --batch --forward --dry-run -p1 -d "$SRC" < "$change" >> "$BASE/patch-check.log" 2>&1; then
   patch --batch --forward -p1 -d "$SRC" < "$change" >> "$BASE/patch-check.log" 2>&1
  else
   patch --batch --dry-run -R -p1 -d "$SRC" < "$change" >> "$BASE/patch-check.log" 2>&1
  fi
 done
 patch --batch --fuzz=0 --dry-run -R -p1 -d "$SRC" < "$HERE/patches/0003-zynqmp-vblank-on-active-modeset-only.patch" >> "$BASE/patch-check.log" 2>&1
 for setting in CONFIG_DRM=y CONFIG_DRM_ZYNQMP_DPSUB=y CONFIG_XILINX_ZYNQMP_DPDMA=y CONFIG_PHY_XILINX_ZYNQMP=y CONFIG_USB_CONFIGFS_F_UVC=y CONFIG_USB_F_UVC=y CONFIG_MTD_SPI_NOR=y CONFIG_SPI_ZYNQMP_GQSPI=y; do
  grep -qx "$setting" "$KOUT/.config"
 done
 python3 "$HERE/tests/test_vblank_restore.py" "$SRC/drivers/gpu/drm/xlnx/zynqmp_kms.c" > "$BASE/vblank-regression.log" 2>&1
 python3 "$HERE/tests/test_dp_hpd_poll.py" "$SRC/drivers/gpu/drm/xlnx" > "$BASE/dp-hpd-regression.log" 2>&1
 # Keep verified configuration; build Image from this isolated patched source.
 cmp "$OLD/kernel/.config" "$KOUT/.config"
 make -C "$SRC" O="$KOUT" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"${JOBS:-8}" Image modules > "$BASE/kernel-build.log" 2>&1
 cp "$HERE/kernel/"*.c "$HERE/kernel/"*.h "$HERE/kernel/Makefile" "$BASE/module/"
 make -C "$SRC" O="$KOUT" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M="$BASE/module" modules > "$BASE/capture-module.log" 2>&1
 cp "$KOUT/arch/arm64/boot/Image" "$KOUT/.config" "$BASE/module/sc130_capture.ko" "$BUNDLE/"
 aarch64-linux-gnu-gcc -O2 -static -Wall -Wextra -Werror "$HERE/flash-boot.c" -o "$BUNDLE/flash-boot"
fi
if [[ $phase == dt || $phase == all ]]; then
 aarch64-linux-gnu-gcc -E -P -x assembler-with-cpp -nostdinc -undef -D__DTS__ -DSC130_ENABLE_CAPTURE -DSC130_ENABLE_CONNECTOR_USB1_PS_USB0 \
  -I "$SRC/arch/arm64/boot/dts/xilinx" -I "$SRC/include" -I "$HERE/dt" "$HERE/dt/sc130-uvc.dts" -o "$BUNDLE/sc130-platform.preprocessed.dts"
 dtc -I dts -O dtb -o "$BUNDLE/sc130-platform.dtb" "$BUNDLE/sc130-platform.preprocessed.dts" 2> "$BASE/dtc.log"
 python3 "$HERE/tests/test_package.py" "$BUNDLE/sc130-platform.dtb"
fi
if [[ $phase == package || $phase == all ]]; then
 aarch64-linux-gnu-gcc -O2 -static -Wall -Wextra -Werror "$HERE/stage-frame.c" -o "$BUNDLE/stage-frame"
 BROKER=${SC130_BROKER:-$HERE/../software/build/sc130_platform_bridge}
 test -s "$BROKER"
 aarch64-linux-gnu-readelf -h "$BROKER" | grep -q 'Machine:.*AArch64'
 ! aarch64-linux-gnu-readelf -l "$BROKER" | grep -q INTERP
 for item in Image sc130_capture.ko sc130-platform.dtb flash-boot; do test -s "$BUNDLE/$item"; done
 ROOTFS=$BASE/rootfs;mkdir -p "$ROOTFS"
 (cd /home/developer/zu4ev/board-linux/rootfs; find . \( -type f -o -type d -o -type l \) -print0 | cpio --null -pdm "$ROOTFS" 2> "$BASE/rootfs-copy.log")
 mkdir -p "$ROOTFS/usr/bin" "$ROOTFS/opt/sc130" "$ROOTFS/sbin"
 install -m755 "$BROKER" "$ROOTFS/usr/bin/sc130_platform_bridge"
 test -s "$(dirname "$BROKER")/dp_smoke"
 install -m755 "$(dirname "$BROKER")/dp_smoke" "$ROOTFS/usr/bin/dp_smoke"
 cp "$(dirname "$BROKER")/dp_smoke" "$BUNDLE/"
 install -m755 "$BUNDLE/flash-boot" "$ROOTFS/sbin/flash-boot"
 install -m755 "$BUNDLE/stage-frame" "$ROOTFS/sbin/stage-frame"
 for script in setup_uvc.sh start-platform.sh tune-irq.sh; do install -m755 "$HERE/$script" "$ROOTFS/opt/sc130/$script"; done
 cp "$BUNDLE/sc130_capture.ko" "$ROOTFS/opt/sc130/"
 install -m755 "$HERE/initramfs-init.sh" "$ROOTFS/init"
 for script in "$ROOTFS/init" "$ROOTFS/opt/sc130/"*.sh; do sh -n "$script"; done
 printf 'nod /dev/console 0600 0 0 c 5 1\nnod /dev/null 0666 0 0 c 1 3\nnod /dev/ttyPS0 0600 0 0 c 204 64\n' > "$BASE/device-nodes.txt"
 "$KOUT/usr/gen_init_cpio" "$BASE/device-nodes.txt" > "$BASE/device-nodes.cpio"
 { (cd "$ROOTFS"; find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --owner=0:0 2> "$BASE/cpio.log");cat "$BASE/device-nodes.cpio"; } | gzip -n > "$BUNDLE/initramfs-sc130.cpio.gz"
 cp "$BROKER" "$BUNDLE/sc130_platform_bridge"
 {
  date -u +'%Y-%m-%dT%H:%M:%SZ'
  aarch64-linux-gnu-gcc --version | head -1
  cat "$KOUT/include/config/kernel.release"
  printf 'kernel=fresh-build isolated-source with approved DRM vblank fix; module=fresh-build; DP=enabled USB=SS-only UVCdefault=120 interval=83333\n'
  printf 'capture=dual-v2 SPI89,90,91,92; USB=GTR1/ref1/26MHz DP=GTR3,2/ref2/27MHz QSPI=unpartitioned32MiB\n'
  sha256sum "$SRC/Makefile" "$SRC/drivers/usb/gadget/function/f_uvc.c" "$SRC/drivers/dma/xilinx/xilinx_dma.c" "$SRC/drivers/gpu/drm/xlnx/zynqmp_kms.c" "$HERE/patches/"*.patch "$KOUT/.config" "$HERE/kernel/"* "$HERE/"*.sh "$HERE/stage-frame.c" "$BROKER"
 } > "$BUNDLE/BUILD-MANIFEST.txt"
 (cd "$BUNDLE"; sha256sum Image .config sc130_capture.ko sc130_platform_bridge dp_smoke flash-boot stage-frame sc130-platform.dtb sc130-platform.preprocessed.dts initramfs-sc130.cpio.gz BUILD-MANIFEST.txt > SHA256SUMS)
 cp -a "$BUNDLE/." "$HERE/dist/"
 gzip -t "$HERE/dist/initramfs-sc130.cpio.gz"
 gzip -dc "$HERE/dist/initramfs-sc130.cpio.gz" | cpio -it > "$HERE/reports/initramfs-members.txt" 2> "$HERE/reports/initramfs-check.log"
 for item in 'init' 'opt/sc130/start-platform.sh' 'usr/bin/sc130_platform_bridge' 'sbin/flash-boot' 'sbin/stage-frame'; do grep -Fx "$item" "$HERE/reports/initramfs-members.txt"; done
fi
cp "$BASE/"*.log "$HERE/reports/" 2>/dev/null || true
case "$phase" in userspace|test|kernel|dt|package|all) ;; *) echo 'usage: build.sh [userspace|test|kernel|dt|package|all]';exit 2;; esac
