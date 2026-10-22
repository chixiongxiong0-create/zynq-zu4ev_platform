# Linux DP integration research

Read-only source audit, 2026-10-20. Kernel root: `/home/developer/zu4ev/sc130-uvc-v2-build/linux-src` in WSL Ubuntu-24.04. Config: sibling `kernel/.config`. No hardware access or implementation performed.

## Decision

Use existing upstream-style `zynqmp-dpsub` DRM driver with its YUYV **video overlay**, atomic KMS, and capture-buffer PRIME import. Keep a black RGB graphics primary framebuffer for reliable CRTC enable; use graphics alpha=0 if required by blend ordering. Do not use legacy CRTC page-flip on a YUYV primary: `zynqmp_kms.c:230-260` makes graphics primary and video overlay. No scaling is supported (`zynqmp_kms.c:54-73`), so capture and display must both be 1280x1024. Keep a copy-backed YUYV overlay path as a diagnostic fallback.

## Existing kernel support

Already enabled: CONFIG_DRM=y, CONFIG_DRM_ZYNQMP_DPSUB=y, CONFIG_XILINX_ZYNQMP_DPDMA=y, CONFIG_PHY_XILINX_ZYNQMP=y, CONFIG_DRM_GEM_DMA_HELPER=y, CONFIG_DRM_BRIDGE_CONNECTOR=y, CONFIG_DRM_DISPLAY_CONNECTOR=y, CONFIG_DMA_CMA=y, CONFIG_CMA_SIZE_MBYTES=256. Audio is enabled but optional to application. No new display kernel driver is required.

`drivers/gpu/drm/xlnx/Kconfig:17-34` requires DMADEVICES, PHY_XILINX_ZYNQMP, XILINX_ZYNQMP_DPDMA and selects the needed helpers. Source files are `zynqmp_{dpsub,dp,disp,kms}.c`; DPDMA implementation is `drivers/dma/xilinx/xilinx_dpdma.c`.

## Device tree

Retain the existing `zynqmp.dtsi` nodes and `zynqmp-clk-ccf.dtsi` clock definitions, enable DPSUB and DPDMA, and supply lane definitions and the missing DP connector graph. Base nodes:

| Label | Address / properties |
|---|---|
| psgtr | fd400000/0x40000 SERDES, fd3d0000/0x1000 SIOU, #phy-cells=4 |
| zynqmp_dpdma | fd4c0000/0x1000, GIC_SPI 122, axi_clk=DPDMA_REF, PD_DP, #dma-cells=1 |
| zynqmp_dpsub | fd4a0000 DP; fd4aa000 blend; fd4ab000 av_buf; fd4ac000 audio, each 0x1000; GIC_SPI119; PD_DP; ZYNQMP_RESET_DP |

Base DPSUB DMA names/order are vid0,vid1,vid2,gfx0,aud0,aud1, mapping DPDMA channel IDs 0..5. Single packed YUYV needs vid0; preserve all six definitions as supplied by base DT. The PM firmware clock/reset/power providers must remain enabled in the standard ZynqMP firmware hierarchy. Do not replace them with fixed register programming.

Actionable board override (merge psgtr clocks with existing USB clocks, do not replace ref1):

```dts
/* Include dt-bindings/phy/phy.h and retain the current USB ref1 26MHz. */
/ {
    dp_ref_clk: dp-ref-clock {
        compatible = "fixed-clock";
        #clock-cells = <0>;
        clock-frequency = <27000000>;
    };
    dp_connector {
        compatible = "dp-connector";
        label = "Mini DisplayPort";
        type = "mini";
        port { dp_connector_in: endpoint {
            remote-endpoint = <&dp_out>;
        }; };
    };
};
&psgtr {
    status = "okay";
    /* Preserve USB's actual clock label in place of usb_ref_clk below. */
    clocks = <&usb_ref_clk>, <&dp_ref_clk>;
    clock-names = "ref1", "ref2";
};
&zynqmp_dpdma { status = "okay"; };
&zynqmp_dpsub {
    status = "okay";
    phys = <&psgtr 3 PHY_TYPE_DP 0 2>,
           <&psgtr 2 PHY_TYPE_DP 1 2>;
    phy-names = "dp-phy0", "dp-phy1";
};
&out_dp {
    dp_out: endpoint { remote-endpoint = <&dp_connector_in>; };
};
```

This maps logical DP lanes0/1 onto GTR3/2 with reference input2, preserving USB GTR1/ref1. Exact board routing is supplied by the proven standalone configuration; validate Linux PHY lane assignment against it. Binding: `Documentation/devicetree/bindings/phy/xlnx,zynqmp-psgtr.yaml` specifies cells lane,type,instance,refclk. DPSUB binding: `Documentation/devicetree/bindings/display/xlnx/xlnx,zynqmp-dpsub.yaml`.

Critical: current base DTS includes ports0..5 but no remote endpoint. `zynqmp_dpsub.c:135-182` therefore does not take its legacy no-ports fallback and rejects the absent port5 connection. Leave live input ports unconnected: this selects DMA mode. Connecting live video would instead require a PL video clock and a different architecture.

## Clocks and mode

`zynqmp-clk-ccf.dtsi:308-320` supplies DPDMA_REF and DPSUB clocks TOPSW_LSBUS, DP_AUDIO_REF, DP_VIDEO_REF, plus assigned DP_STC_REF/DP_AUDIO_REF/DP_VIDEO_REF. Clock IDs: video16, audio17, STC18, DPDMA20. `zynqmp_disp.c:1374` calls clk_set_rate(video clock, mode clock in Hz). The 27MHz transceiver reference and 187315200Hz pixel source are separate clocks.

Use the monitor EDID mode if exact verified 1280x1024@120 exists; otherwise construct the proven standalone timing as a DRM mode blob (copy every porch, sync polarity and total). DRM clock field is integer kHz, so 187315200Hz becomes187315kHz, a tiny rounding difference. Do not assume Linux can synthesize that rate from inherited VPLL settings: inspect clk_summary/actual DP_VIDEO_REF after modeset and confirm measured 120Hz. Firmware-controlled clock tree may require assigned parent/rate setup matching standalone VPLL. DP driver mode_valid checks maximum pixel frequency and lane/rate bandwidth (`zynqmp_dp.c:1529`), then performs normal link training. This mode needs about4.496Gb/s at24bpp before DP overhead; two HBR2 lanes have8.64Gb/s payload and should suffice if the sink negotiates them.

## Buffer and event ownership

`zynqmp_disp.c:198-230` explicitly supports DRM_FORMAT_YUYV with no U/V swap. Use capture's bytesperline, width1280,height1024, fourccYUYV, offset0. Import each V4L2 MMAP buffer once: VIDIOC_EXPBUF -> DRM_IOCTL_PRIME_FD_TO_HANDLE -> DRM_IOCTL_MODE_ADDFB2. Cache GEM handles/fb IDs for the lifetime of the allocation; do not import each frame.

The GEM DMA helper rejects a PRIME sg-table whose contiguous mapped extent is smaller than the full dmabuf (`drm_gem_dma_helper.c:445-485`). CMA-backed vb2 DMA-contiguous capture should meet this, but prove with actual import. DPDMA base address requires256-byte alignment; copy_align is8, so DRM pitches are aligned to256 (`xilinx_dpdma.c:134,772,1774`; `zynqmp_kms.c:443-467`). 2560-byte rows are already aligned. The framebuffer creation helper silently rounds the passed pitch: never submit an unaligned real capture pitch or it will interpret memory incorrectly.

Enable DRM_CLIENT_CAP_UNIVERSAL_PLANES and DRM_CLIENT_CAP_ATOMIC. Enumerate connector/CRTC/planes and supported format instead of assuming object IDs or card0. Set connector CRTC_ID, CRTC ACTIVE/MODE_ID, primary black RGB FB_ID/CRTC_ID and geometry, and YUYV overlay FB_ID/CRTC_ID plus SRC_X/Y/W/H (16.16) and CRTC_X/Y/W/H. TEST_ONLY|ALLOW_MODESET first; then initial blocking ALLOW_MODESET. Thereafter submit one outstanding atomic commit at a time with NONBLOCK|PAGE_FLIP_EVENT and change overlay FB_ID. Handle DRM fd events via read/drmHandleEvent and per-commit token.

On commit of B replacing A, its page-flip event releases **A**, not B. B remains scanout-owned until the event for a later replacement C, or until a confirmed synchronous plane/CRTC disable. `zynqmp_kms.c:343-359` arms the event for vblank; DP IRQ calls drm_crtc_handle_vblank at433. Never QBUF a current or pending DP buffer. USB and DP are independent consumers: recycle capture buffer only after both have released their references. No USB host/STREAMON event should control camera startup or DP startup. When DP is slower or hot-unplugged, drop superseded queued display candidates; never block acquisition waiting for a host. On disconnect, disable display plane synchronously and release its current/pending references after completion. Flush/error recovery must not mistake a disable-completion event for evidence that the new framebuffer is actively scanning.

Because the existing capture uses noncoherent cacheable mappings, DMA-BUF attachment and synchronization need real validation. Pure DMA capture->DP avoids CPU writes; CPU reads for USB may need exporter synchronization. Preserve existing proven USB cache-hint behavior, and do not infer that CPU coherency is solved merely because PRIME import succeeds. Tone mapping in PL before capture preserves the zero-copy DP path.

## Copy fallback and build

Allocate at least three DRM dumb buffers at16bpp, use returned pitch/size, ADDFB2 as YUYV, mmap and copy each row from capture into an available non-current/non-pending buffer. Atomic overlay commit semantics stay identical. This isolates capture/USB ownership and PRIME/coherency problems. At1280x1024x2x120, one image stream is314.57MB/s; a CPU copy entails at least629MB/s memory traffic plus capture writes and DP reads, so 120fps cannot be assumed. It is useful for bring-up, not an automatic performance-equivalent replacement. The dumb buffer mapping is a DRM DMA allocation and may be write-combined; benchmark actual CPU copy speed.

WSL has `/usr/bin/aarch64-linux-gnu-gcc`, cross libc.a, and `/usr/aarch64-linux-gnu/include/drm/{drm.h,drm_mode.h,drm_fourcc.h}`. `pkg-config --modversion libdrm` fails and no cross libdrm library was found under `/usr/aarch64-linux-gnu`. Small static app can use DRM ioctls directly with these kernel ABI headers (no libdrm dependency); otherwise cross-build a pinned libdrm into a separate local prefix with meson static libraries and disabled unrelated GPU drivers/tests, then link libdrm.a. This audit did not download or install anything.

## Verification gates

1. Boot same USB-capable Linux image with DP DT enabled; inspect probe errors, connector status, modes, clocks and PHY lane ownership. USB SuperSpeed remains operational.
2. Display black/RGB dumb framebuffer with no USB host attached, then YUYV test pattern, then capture copy path. Confirm exact120Hz sink mode and colors.
3. PRIME import each capture buffer; test-only atomic overlay; sustain page-flips with generation-tagged ownership logging and no overwrite/requeue of scanout buffers.
4. Run DP-only, USB-only, both, USB disconnect/reconnect and DP hotplug. Record source, DP flips and USB completions separately over long intervals, along with underruns and dropped candidates.
5. Only after combined sustained119.966fps and autonomous boot pass should this image become the QSPI candidate. This report establishes source-supported feasibility, not board validation.
