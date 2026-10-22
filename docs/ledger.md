# Implementation ledger

- User authorized autonomous integrated final build, QSPI programming and validation, while asleep.
- Ruling: use Linux for both outputs; standaloneDP plus LinuxUSB cannot own PS peripherals simultaneously safely. Existing kernel includes DRM/DPSUB/DPDMA.
- Ruling: use DP single-load hardware as baseline; enable USB0 on GTR1, preserving DP3/2 and independentCSI resets.
- Ruling: compressed FIT via existing pinned2025.1 U-Boot fits32MiB; uncompressed aggregate does not.
- Ruling: use direct DRM ABI without libdrm dependency; overlay supportsYUYV, primaryblackRGB; pageflip retires ownership.
- ISP unit tests pass3422cycles, exactvendorLUT; source isolated underisp/.
- Vivado MCP startup fails before any output; localVivado2025.1 batch is available and used. No originalproject mutation intended.

- RAM验证定位VPLL EUSERS：PMU对所有已选PLL子时钟计数，未使能audio/STC也计入。FSBL在psu_init成功后、XFsbl_PmInit前，将DP_AUDIO/STC的SRCSEL改为RPLL_TO_FPD(3)，保留VIDEO使用VPLL。原始生成PSU文件不改，覆盖逻辑集中于prepare_fsbl.py。
- Linux2025.1 DRM驱动每次atomic_begin调用vblank_on，导致软件序号每帧额外加1。0003补丁仅在active且needs_modeset时恢复vblank，保留启停顺序。
- 改写前完整32MiB QSPI备份SHA2567163d9a12cd3fd2cccb16d824a1c75aec163ec3f0b1965696f93c8a013673c56，与板端/dev/mtd0散列一致。
