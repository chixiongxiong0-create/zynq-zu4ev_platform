# 构建边界与依赖

## FPGA

Vivado/Vitis 2025.1，ZU4EV sfvc784 -2 工业级。使用 tools/recreate_project.tcl 和 tools/build_platform.tcl。hardware/*.tcl 来自当前实板工程；重建重新生成 IP，不携带增量 DCP。MIPI CSI-2 接收 IP 许可由本机 AMD 工具提供。

## 启动固件

根据新生成的 XSA 生成 FSBL 源码、standalone BSP 和 PMUFW，使用 boot/fsbl/build_fsbl.ps1 的 SourceDirectory/BspDirectory 参数构建定制 FSBL。ATF、U-Boot 脚本内记录使用的外部源码目录和配置。不要混用不同 PS 配置生成的 BSP/psu_init。

## Linux

linux/build.sh 当前采用原开发机 WSL 的专用目录 /home/developer/zu4ev/sc130-platform-build；初始 linux-src、kernel/.config 来自相邻 sc130-uvc-v2-build，基础 rootfs 来自 board-linux/rootfs。这些外部构建树不随源码上传。迁移时须先提供同版 AMD Linux 内核和配置、基础根文件系统、AArch64 GCC、make、dtc、cpio、gzip 与 U-Boot mkimage，再调整脚本路径；内核补丁在 linux/patches 按序应用。现有代码是已验证源码快照，而不是完整 Linux 发行包。

分阶段构建时，package 阶段显式传入本次编译的应用：

```bash
bash linux/build.sh userspace
SC130_BROKER=/home/developer/zu4ev/sc130-platform-build/software/sc130_platform_bridge bash linux/build.sh package
```

完整 all 阶段在同一进程传递该变量。软件测试入口 make -C software test；ISP 仿真见 sim/isp/run.ps1；QSPI 打包检查见 boot/test_package_qspi.py。实板验证报告包含历史绝对路径和未随 Git 发布的日志/图像引用，供追溯使用。

## 发布与验证

原始 sc130_platform 工作目录保持不变。本仓库只整理当前集成版本；历史仅 DP 工程未混入。最新实板证据见 reports/dp_hpd_boot_recovery.md、reports/black52_isp_validation.md、reports/dma_address_fix_validation.md。用户实际手动断电重启和长期 USB 无丢帧仍需后续验证。
