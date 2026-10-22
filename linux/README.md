# Unified SC130 Linux package

Build in WSL Ubuntu-24.04:

    bash /mnt/e/workspace/AI_Projects/ZU4EV/sc130_platform/linux/build.sh all

Dedicated build root: `/home/developer/zu4ev/sc130-platform-build`. Optional phases: userspace, test, kernel, dt, package. Package-only accepts `SC130_BROKER=/absolute/path/sc130_platform_bridge`; the same directory must contain static `dp_smoke`. Defaults use `../software/build`. All builds userspace into the isolated WSL build root. No hardware, boot, flash, JTAG or serial operations occur in build.sh.

Prerequisites are the existing verified `/home/developer/zu4ev/sc130-uvc-v2-build/{linux-src,kernel}`, baseline BusyBox `/home/developer/zu4ev/board-linux/rootfs`, AArch64 GCC, dtc/fdtget, cpio and gzip. Kernel source and output trees are independently copied with reflink where available, never hardlinked. The verified configuration and required DP/UVC/QSPI features are retained. Patches are applied idempotently or reverse-checked; approved DRM patch0003 additionally requires an exact reverse check with fuzz0. Image and modules are now built freshly from the isolated patched kernel tree. Driver sources are copied into the isolated module directory and compiled freshly against kernel6.12.0. This is a reproducible local build recipe using the pinned existing kernel, not a network source download or from-scratch upstream build.

Inputs copied from linux-v2: kernel/sc130_capture.c and contracts, configuration fragment, SS-only UVC patch, VDMA halted query patch, corrected PL capture DT and current gadget setup. Broker comes from the independent platform/software implementation. Current fixes include noncoherent MMAP/cache hints, correct DMA-BUF error handling and independent physical DMA attachments. No source in the verified project was modified.

DT enables both capture masters and one logical capture node, with GIC SPI89/90/91/92 (not full INTID121..124). DP+DPDMA use existing Linux DRM drivers, DP lanesGTR3/2 and reference2 at27MHz. USB remainsGTR1/reference1 at26MHz and USB0 peripheral SuperSpeed. A mini DP connector graph is present; live input ports remain disconnected for DMA mode. QSPI is enabled with a single JEDEC NOR and no partition nodes so the full32MiB device is mtd0. Actual NOR size is enforced by the flash helper at runtime. DDR0x30000000..0x34000000 remains reserved for maintenance staging. The platform expects matching FSBL/PMUFW/bitstream and sensor initialization from the main boot flow.

The initramfs mounts runtime filesystems, starts /opt/sc130/start-platform.sh in the background and leaves a console shell. Startup loads sc130_capture.ko, prepares SS-only YUY2 descriptors, binds an available UDC disconnected and starts sc130_platform_bridge. Broker owns soft-connect after event subscriptions and starts capture and DP independently of USB STREAMON. Gadget setup/UDC absence does not gate broker startup. Default UVC descriptor interval83333 matches broker default120;166667 is supported for host60 negotiation. No HS/FS streaming descriptor links exist. Existing lab VID/PID1d6b:0102 are retained from the proven setup and may be overridden in /etc/sc130-platform.conf. Startup log is /tmp/sc130-platform.log. Dynamic IRQ discovery directs dwc3/fe200000.usb IRQs to CPU3 if online. There is no fixed Linux IRQ number.

Installed diagnostics: /usr/bin/dp_smoke and /sbin/flash-boot. The latter is built from the inspected existing flash-boot.c, supports --info, --backup and bounded --write, and is never called by startup. Its sourceDDR0x32000000 and backupDDR0x30000000 remain reserved. It requires exactly32MiB NOR with valid erase geometry.

Output dist contains Image, sc130-platform.dtb, initramfs-sc130.cpio.gz, .config, sc130_capture.ko, sc130_platform_bridge, dp_smoke, flash-boot and manifest/checksums. The generated initramfs is sorted and gzip has no timestamp header; inherited rootfs/file timestamps mean byte-identical rebuilds across changed inputs/times are not promised. SHA256SUMS identifies each produced build.

Offline validation (2026-10-20):

- Fresh module compilation succeeded, vermagic6.12.0 SMP mod_unload aarch64; kernelImage was retained byte-identical to baseline for the initial package; the later approved DRM fix below supersedes that Image.
- Full native software regression: broker ownership, DP retirement/quarantine, USB stop independence and default120 tests passed. Driver dual-core regression passed owners, channel joining, generation, reordering, bounded gaps, wrap and STOPPING.
- Fake-configfs tests passed exact YUY2 GUID/frame size, SS-only links, default83333 and advertised83333/166667, and unbound setup.
- DT compilation emitted no warnings. fdtget assertions passed enabled DP/DPDMA/USB/capture/QSPI, both reference frequencies, GTR lane cells, reciprocal DP endpoint and capture interrupts/reserved region.
- Shell syntax checks passed; gzip integrity, cpio listing and required initramfs members passed. All10 SHA256SUMS entries passed.
- Static AArch64 ELF checks passed; flash-helper QEMU no-argument test prints usage and returns2 without opening hardware.

Artifacts/logs are in reports/. No board boot, display link/clock training, PRIME import, UVC enumeration, measured120fps, unplug recovery or flash write is asserted by these offline tests. Those acceptance gates remain the main task's hardware validation.

## Manual frame snapshot export

Updated package includes SIGUSR1 snapshot support in the broker and /sbin/stage-frame. Run `kill -USR1 <broker-pid>`, wait for a SNAPSHOT log confirming the completed2621440-byte /tmp/sc130-frame.yuy2, then run `/sbin/stage-frame` (or pass another filename). It verifies a regular file of exactly2621440bytes, reads the entire file before opening /dev/mem, maps reservedDDR0x30000000 for2621440bytes, copies with volatile byte accesses and compares every byte read back. Success marker: SC130_FRAME_STAGED_OK. Main task may then download this DDR range through JTAG for image analysis. Neither snapshot nor staging runs automatically.

Additional offline tests pass: real copy/compare over a full2621440-byte host buffer with guard bytes, wrong sizes0/2621439/2621441 rejected before /dev/mem, directory rejection and extra-argument usage. This verifies logic; actual device-mapped DDR staging remains a board test. Rebuilt broker's four native regressions pass, including snapshot signal, exact size, I/O failure and ownership retention. All11 output checksums pass after repackaging. Kernel, module and DT remain unchanged.

Final initramfs SHA256: ffd8bfee67f9fce450d1c147c8ae3d19335b634711faafad97207e17e38921f9

Final broker SHA256: 0fbc4ab6f241939883b3f57c0412b9c54ea476692444d2084f6677280f5487d1

## Kernel vblank restoration correction (2026-10-21)

Approved patch0003 now gates drm_crtc_vblank_on() in atomic_begin to an active CRTC that needs a modeset. Ordinary plane/framebuffer flips preserve the software vblank counter; active activation/resume, mode changes and connector changes restore it. The call remains before atomic_flush arms the initial event. There is no change to physical display timing or buffer ownership rules.

The isolated kernel was rebuilt through Image and modules, then sc130_capture.ko was rebuilt against that output. Final idempotent build verification succeeds and module vermagic remains6.12.0 SMP mod_unload aarch64. The configuration, DT and broker are unchanged. Kernel reuse statements above describe the historical initial package only; current build.sh compiles the patched Image. Manifest includes all three patches and zynqmp_kms.c hash.

The source-extracted regression tests the production atomic_begin callback with small DRM helper substitutes:120 ordinary flips make0 restoration calls; active activation, mode change and connector change make1 each; inactive disable makes0. The original callback fails this test; patched callback passes. Reproduce with tests/test_vblank_restore.py and the isolated kernel zynqmp_kms.c path. Red/green transcripts are reports/vblank_regression_red.log and reports/vblank_regression_green.log; the current build's check is reports/vblank-regression.log. Exact reverse application of patch0003 with fuzz0 passes.

The broad first rebuild produced one unrelated warning in drivers/misc/xlnx_dpu.h:21: macro in_range redefined. No error occurred; the final incremental build and module compilation pass. The first build script resumed incorrectly after the recipe was edited while it was running; rerunning the stable recipe completed module/package stages successfully. Current reports/kernel-build.log is that final incremental verification log.

Current output hashes (supersede earlier package hashes):

- Image: 69f52192c1eecc6f68fcbe73984149aca345e584816fbc67b092ff664066b8bb
- initramfs-sc130.cpio.gz: 71741d7222ba6b8bcdd69df1882ff46e747eee1ff1e5a850fbe6602b0fb6bdb5
- unchanged DT: f0cd7ff70c5a5449ebe18ae5c2e79b39806c1451ad6ef48745f7bafba6e17c23
- unchanged broker: 0fbc4ab6f241939883b3f57c0412b9c54ea476692444d2084f6677280f5487d1

All11SHA256 checks, archive integrity/member checks, driver dual-core, fake gadget and staging helper tests pass. Actual vblank sequence behavior with this Image still requires main-task board validation. The separate FSBL clock-parent correction is outside this Linux source change; no unsupported DT assigned-clock-parents properties were added.
