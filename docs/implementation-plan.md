# SC130 unified platform implementation plan

**Goal:** QSPI-autonomous camera ISP → simultaneous DP120 and USB3 UVC120, usable as an ISP learning baseline.

**Architecture:** One validated dual-VDMA capture path, static LUT ISP, Linux DRM overlay imports and independent USB copy output. Fresh project and boot artifacts preserve earlier working projects.

**Spec:** design.md. User selected autonomous execution; no repeated approval gates. Independent ISP, DRM and boot work has isolated ownership; main owns hardware, integration, serial and flash.

## Tasks

- [x] Audit attachment, lane mapping, kernel support and flash capacity.
- [x] Implement/test ISP adapter, exhaustive RAW10 and timing coverage.
- [x] Clone validated single-load PL project; enable DP+USB0, replace converter, build and inspect timing/DRC and XSA.
- [x] Enable matching Linux DP PHY/connector/DPDMA plus corrected capture IRQs; generate matching firmware and RAM image.
- [x] Implement DRM DMA-BUF overlay with flip-retirement ownership; add tests for pending/active/release and failure handling.
- [x] Integrate capture broker: DP boot preview independent of USB; test stream-on/off/disconnect and buffer ownership; preserve cache synchronization.
- [x] Build pinned fixed-QSPI U-Boot and bounded FIT/BOOT.BIN/MCS packaging; test size/range/identity validation.
- [ ] RAM boot and actual simultaneous camera/ISP/DP/UVC tests; diagnose failures with boundary telemetry.
- [ ] Save recovery image, program verified full image, read back hashes, reboot and validate autonomous outputs; retain evidence and learning documentation.

## Review focus

- USB unplug/close cannot requeue a DP active/pending page.
- DP hotplug/disabled mode cannot free referenced pages early.
- CSI reset/control and fault metadata remain aligned with one-stage ISP.
- Boot image bounds and Linux reservations cannot overlap or exceed32MiB.
- Capture sync is per actual DMA master; no coherency shortcuts for speed.

