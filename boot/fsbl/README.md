# Custom standard-boot FSBL

Run build_fsbl.ps1 with SourceDirectory containing fresh generated xfsbl_hooks.c and lscript.ld, and BspDirectory containing matching include and lib directories. Generated firmware remains untouched. Prepared sources, objects, ELF, map and layout report are written below fsbl/build by default. No RAM/JTAG bypass is compiled.

The bitstream hook runs once after PL DONE and isolation removal. It requests PL power bit23, calls pipeline hold (including GPIO bank5 bit31 PS reset release), configures and verifies sensor standby, releases pipeline lite/video/capture, then streams on. Handoff requires one successful PL hook and SENSOR_READY. No DDR cookie addresses are used.

The PSU hook first requests USB0 PMU power bit20 using direct writes to write-only ENABLE_SET ffd80118 and TRIGGER ffd80120. It polls ffd80100 at most 10,000,000 iterations without using the not-yet-initialized system timer and returns failure before generated psu_init can access unpowered USB MMIO. Already-powered path performs no writes. Other enabled request bits are preserved. Normal BootROM PMU ROM/PMUFW service is required.

Offline checks: python test_prepare.py validates hook replacement and OCM overlap detection. gcc tests/test_power.c verifies powered fastpath, bounded failure, delayed success, write-only register discipline, timer independence and request ordering. Preparation exercised against pristine installed 2025.1 vendor FSBL; prepare_test is an offline fixture only. Final matching source+BSP build awaits main task generation.

Matching generated build completed: build/fsbl_sc130.elf SHA256566d2940df2451b9fea14f58bb137d6fd509d9fc233ba8bbf5559bd8b0bd5b03. Main code/data/stack endfffDC378; handofffffe9e00 size88; temporary PCAP bufferffff0040 sizefc00. Link and OCM checks pass.

RAM test: add -RamTest to build_fsbl.ps1. Separate build/ramtest ELF redirects only QSPI32 reads into a bounded32MiB staged image at30000000. Cache disabled before Sc130RamBootReady breakpoint. Current RAM-ready symbolfffc5754, ELF SHA256de16086ee6a085c70372cf9956783c04416ab2b98ec834e0d91ff35b278c8884. Stage the complete normal BOOT.BIN at30000000 after breakpoint, resume for normal PL/sensor/ATF/U-Boot handoff. The production U-Boot still reads FIT from flash; the main task must intercept it to boot its staged FIT. No hardware tests were run by this component.

DP clock ownership override: after successful generated psu_init and before XFsbl_PmInit/PMU PmClockInit, HookPsuInit sets only SRCSEL[2:0] ofDP_AUDIO_REF_CTRL FD1A0074 andDP_STC_REF_CTRL FD1A007C to3(RPLL_TO_FPD), readbackchecks both. DP_VIDEO staysVPLL. PMU counts selectedparents even for disabledoutputs, so leavingallthree onVPLL blocksLinux videoPLLratechanges withEUSERS. GeneratedPSUsources remainbyteidentical toXSA. Test_prepare nowchecks hookordering andprotectedvideoaddress.

2026-10-21: pre-PSU power request no longer reads ENABLE_SET or calls usleep. New production FSBL SHA256 750a53325641432be57a19ad590f5f7ced9c967d5bc5c635b21b9723048f248b; RAM FSBL SHA256 2d999a0ac36415a1f8dfb30a49144e1a67ecc1a646ecbe054907ff5b620f7250; RAM-ready symbol fffc5728. Earlier hashes and symbol values above describe historical builds only. See ../../reports/dma_address_fix_validation.md for current validation status.
