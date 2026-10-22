# Independent integration review — 2026-10-20

Scope: offline review only. No JTAG, serial, flash, or hardware operations. Reviewed source under `sc130_platform` against the stated design and selected DP camera baseline. No production files changed.

## Findings

No confirmed critical or important correctness defect found in the reviewed snapshot. This is an offline code result, not approval that simultaneous 120 fps or autonomous QSPI boot has been demonstrated.

## Reviewed integration boundaries

- `rtl/uvc_capture_dual.v:126`: packet/global fault registers delay the raw-source flags exactly one cycle, matching the registered ISP output. Both channel gates use the delayed flags. The converter is permanently ready at this integration point, so elastic backpressure cannot desynchronize those flags.
- `isp/raw10_to_yuy2.v:1037`: BYPASS uses the same limited-range arithmetic as `sc130_dp_test/camera/rtl/raw10_to_yuy2.v`; normal mode emits neutral chroma and the static ROM curve. SOF/EOL are qualified with valid.
- `software/platform_bridge.c:141`: USB stop only clears its independent output queue and policy. Latest capture and DP references survive. USB copying is synchronous on the sole event-loop thread, so it cannot race capture recycling.
- `software/platform_bridge.c:267` and `software/dp_display.c:283`: an accepted display submission acquires a capture reference; one pending commit is enforced; only a matching page-flip event releases the former active page. Failed display disable retains the object, imports, and broker references. Shutdown waits before freeing mappings.
- `linux/kernel/sc130_capture.c:181,208,268`: per-VDMA DMA-BUF mappings and CPU/device synchronization remain present. The userspace broker requests cacheable pages without skip-sync flags.
- `tools/configure_platform.tcl` and `reports/ps_readback.txt`: USB0 uses MIO52–63, GTR1/ref1/26 MHz and MIO36 reset, while DP retains GTR3/2/ref2/27 MHz. Linux PHY selections match. Capture IRQs are SPI89/90/91/92 rather than the old ambiguous mapping.
- `boot/fsbl/prepare_fsbl.py`: generated PSU initialization and normal handoff code remain; the hooks add bounded power requests and one-load sensor initialization. `sc130_sensor_start.c` rejects zero or multiple PL loads and does not substitute a direct BL33 jump for ATF handoff.
- `boot/package_qspi.py`: BOOT is bounded to 12 MiB and FIT to the remaining 20 MiB. The U-Boot script reads that exact flash range. Kernel, FDT and initramfs destination limits avoid their stated staging ranges. Linux reserves maintenance DDR 0x30000000–0x33ffffff. CMA is configured to 256 MiB even though U-Boot replaces DT bootargs.

## Repeated offline checks

- `python boot/test_package_qspi.py`: 2 tests passed.
- `python boot/fsbl/test_prepare.py`: 3 tests passed.
- `make test` in `software`: broker, DP ownership (including failed-disable quarantine), and USB-stop/default120 tests passed with warnings treated as errors.

## Remaining acceptance gates before final flash

These are required evidence, not newly discovered source defects:

1. Demonstrate the exact packaged artifact booting through FSBL → PMUFW → ATF → U-Boot → Linux with a single PL load and sensor-ready marker. A JTAG RAM-test FSBL alone does not establish cold QSPI behavior.
2. Record actual display flip/new-frame rate separately from capture and USB-return rates. The selected 120 Hz mode or average capture fps does not prove 120 distinct displayed frames.
3. Exercise host STREAMOFF, reconnect, and DP unplug/replug while capture runs. Offline tests directly cover STREAMOFF and ownership bookkeeping; physical link state and blocking atomic-disable behavior require the board.
4. Confirm final BOOT.BIN uses the matching final XSA/bitstream FSBL/PMUFW, then save the manifest and flash readback hashes. Source-level range checks alone do not establish artifact identity.


## Final delta addendum — 2026-10-21 07:55 CST

No new critical or important source defect identified in this delta. Reviewed the FSBL clock-parent patch, DRM vblank patch, delayed IRQ affinity startup, diagnostic USB sequence marker, and final package identities offline.

- `boot/fsbl/prepare_fsbl.py:6`: clock-parent changes occur after successful generated PSU initialization in HookPsuInit and before PM initialization. MAIN source switches to DPLL while preserving its generated divider; readback requires DPLL/3 (400 MHz). Audio/STC source changes preserve divider/control fields. Video remains on VPLL. These separate fixed interconnect consumers from the variable DP pixel PLL before PM firmware records children. RAM final UART records the parent marker, SINGLE_PL count=1 and SENSOR_READY; no WARN line was found in the reviewed log.
- `linux/patches/0003-zynqmp-vblank-on-active-modeset-only.patch`: restores vblank only for an active modeset; framebuffer-only flips no longer reset vblank bookkeeping. This addresses synthetic event/count behavior, not a proven cause of the reported motion ghosting.
- `linux/start-platform.sh:21` and `linux/tune-irq.sh`: affinity discovery runs in the background, waits at most 30 seconds for the dynamically allocated USB IRQ, and cannot block camera/DP startup.
- `software/platform_bridge.c:207`: optional marker is off by default and writes only eight YUY2 pixel pairs in the independent USB output copy after memcpy. Capture/DP storage and ownership are unchanged. Marker values deliberately include diagnostic luma outside nominal video range; this is confined to explicit diagnostic mode.

Repeated FSBL tests: 4 passed. Repeated full software tests: broker, DP ownership, USB events, snapshot and marker tests passed. All Linux distribution SHA256SUMS entries passed. Every SHA256 record in BUILD-MANIFEST matches the current source/artifact path, including the patched KMS source and marker-capable broker.

Package inspection independently verified all three manifest lengths/hashes, gzip-expanded packaged kernel equals linux/dist/Image, packaged DTB/rootfs equal linux/dist counterparts, BOOT prefix and FF padding are exact, and FIT starts exactly at 0x00c00000. BOOT.BIN=9,091,376 bytes; FIT=13,347,240 bytes; complete image=25,930,152 bytes, within 32 MiB. Final image SHA256: ccd7af00f55506ae975033d7d7bdc58856002ceae85c39901bc41531af8ddcb8. BIF selects normal FSBL, matching platform PMUFW/bitstream, ATF and U-Boot. The package manifest does not itself record each BIF input hash, so exact BOOT subpartition provenance still relies on the build receipt/current BIF and bootgen result.

Integrator-reported board evidence: FFmpeg received 1,801 frames in 15 seconds; the 20-second source-marker capture contained 2,399 frames, no bad marker/duplicate/backward sequence, and two isolated forward gaps. Those results support near-120 distinct source delivery with occasional drops; they do not establish mathematically zero-drop operation.

The user-reported motion ghosting remains open. Static DDR and host PNG checks being clean do not prove motion quality is fixed. Do not describe the clock/vblank changes or marker run as resolution of the ghosting report without the user's static-versus-motion clarification and matching motion evidence. Cold autonomous QSPI reboot/readback remains a separate acceptance gate.
