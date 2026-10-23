# Zynq ZU4EV SC130GS Camera Platform

SC130GS 摄像头学习平台：MIPI RAW10 → FPGA 黑电平校正与 Tone Mapping → DDR → DP 120 Hz / USB 3.0 UVC YUY2。

当前源码快照对应 2026-10-22 `black52-hpd` 实板版本。DP 1280×1024@120 Hz、单次 PL 加载、QSPI 复位后自动显示已验证；USB 最新连续窗口约117 fps，仍存在少量完成错误，后续需继续优化。此仓库不宣称 USB 无丢帧。

## 获取及构建

使用 Vivado/Vitis **2025.1**，器件 `xczu4ev-sfvc784-2-i`。CSI-2 RX IP 需要本机可用的相应 AMD IP 授权。

```powershell
git clone https://github.com/chixiongxiong0-create/zynq-zu4ev_platform.git
cd zynq-zu4ev_platform
vivado -mode batch -source tools/recreate_project.tcl
vivado -mode batch -source tools/build_platform.tcl
```

`hardware/` 是从当前工程直接导出的 PS（包含 DDR、DP、USB）、CSI、时钟与 ILA 配置。重建不依赖旧工程的 DCP 或相邻目录。上述入口重新生成工程；`tools/clone_project.tcl`、`configure_platform.tcl` 是历史维护脚本，含原开发机路径，不作为移植入口。

PL 构建生成 `dist/sc130_platform.xsa`。用 Vitis 根据此 XSA 生成匹配的 standalone FSBL 和 BSP，然后按 [FSBL说明](boot/fsbl/README.md) 编译定制启动代码。Linux、ATF、U-Boot 和根文件系统另需外部源码与交叉工具链；参见 [构建依赖说明](docs/BUILD_DEPENDENCIES.md) 及 [Linux说明](linux/README.md)。当前 Linux/启动脚本保留原开发机路径，不能宣称任意机器一键完整构建。

源码仓库包含 RTL、ISP 原始附件与生成器、内核补丁、驱动、应用、测试、启动与打包代码、验证报告。生成的 bitstream/XSA/ELF/MCS、工具缓存、板卡 Flash 备份和摄像头实拍数据不进入 Git。`dist/black52-hpd/manifest.json` 记录已验证镜像身份；仓库中的 release 说明指向本地验证产物，不代表这些二进制已随源码发布。

---


本目录是独立集成工程。目标为 SC130GS 的 1280×1024、约120 fps真实图像，经 FPGA 暗部色调映射后，同时送往 Mini DP 显示器和电脑 USB3 UVC 摄像头。构建结果与板级验证结果分别保留在 `dist/`、`reports/`，最终以板级验证记录为准。

## 图像怎样流动

```text
SC130GS RAW10 / 双 lane MIPI
  → D-PHY / CSI-2 / 两像素并行
  → isp/isp.v统一入口 → raw10_to_yuy2.v包装 → sc130_tonemap_yuy2：RAW黑位52校正＋静态亮度LUT，U=V=128
  → 两路交替整帧采集、VDMA写DDR
  → Linux V4L2完成帧队列
      ├─ DMA-BUF → DRM YUYV视频图层 → PS DPDMA → DP 120Hz
      └─ CPU读取已完成帧 → UVC输出缓冲 → PS USB0 SuperSpeed → PotPlayer
```

DP 使用采集缓冲区的直接导入；USB 路径包含一次 CPU 拷贝。YUY2 每帧2,621,440字节，120fps有效数据约314.57MB/s。摄像头从启动开始持续采集，电脑打开或关闭摄像头只控制 USB 输出。

DP 当前扫描帧和等待切换帧都保持占用，收到后续切换事件才释放旧帧。只有既不是最新候选帧、又不被 DP 使用的缓冲区才重新交给采集。这部分规则位于 `software/capture_broker.h`，是以后添加 ISP 算法时应保留的边界。

## 工程入口

| 目录 | 内容 |
|---|---|
| `rtl/` | 摄像头顶层、CSI控制、独立复位、整帧守卫和双VDMA采集控制 |
| `isp/` | 实际综合使用的RAW10→YUY2色调映射；附件原始文件在vendor中 |
| `reference/` | 原来的线性转换器，仅作学习对照，不参与综合 |
| `constraints/` | 已核对的板级管脚和时钟约束 |
| `tools/` | Vivado配置、构建、RAM启动和串口记录工具 |
| `software/` | 持续采集、DP显示和UVC事件处理程序及缓冲区测试 |
| `linux/` | 板级设备树、采集驱动、USB描述符和根文件系统构建 |
| `boot/` | 单次PL加载FSBL、PMU配套启动、ATF、U-Boot及QSPI打包工具 |
| `sim/isp/` | ISP穷举输入和时序仿真 |
| `docs/` | 设计决定、实施计划和变更记录 |
| `reports/` | 构建、仿真、检查和实板验证证据 |

## 暗光处理及学习入口

当前 ISP 实际调用由附件整理的 `sc130_tonemap_yuy2`：先扣除遮光测得的RAW10黑位52，截断负值，再将剩余971码归一化后通过附件曲线映射至亮度16～235。黑位校正与曲线合并为1024项LUT，保持一级流水线。它没有增加曝光时间，也没有执行降噪，暗部噪声可能随亮度一起变明显。

每拍接受两个像素，输出内存字节顺序为 `Y0 80 Y1 80`，一级流水线。`BYPASS=1` 可在重新综合时选择原来的线性映射，便于做算法对照。LUT 内嵌 RTL，烧录后运行不依赖外部 `.mem` 文件。修改曲线、生成器和完整仿真方法见 `isp/README.md`。

建议依次学习：黑位校正 → 固定亮度曲线 → 帧统计 → 自动曝光/增益 → 自适应曲线 → 空间降噪。修改算法时保持两像素吞吐率、SOF/EOL和故障标记的延迟一致，再检查时序和实际帧率。

## 启动和硬件连接

板卡为 ZUMINI ZU4EV，SC130连接原MIPI口，显示器使用 Mini DP→DP 直连，电脑连接板上标为USB1的接口。该接口对应芯片的 **PS USB0**。USB需要支持SuperSpeed的数据线。

PS高速通道配置：DP使用GTR3/2和27MHz参考2；USB使用GTR1和26MHz参考1。两者共享PS但不占用同一条高速lane。

QSPI镜像包含FSBL、PMU固件、一次PL配置、ATF和U-Boot；FSBL先唤醒USB电源域，再执行PS配置。PL完成后执行传感器待机配置、独立CSI/采集复位释放和开流，随后进入Linux。Linux启动显示和UVC服务。完整启动不需要传感器配置后再次加载PL。

32MiB QSPI布局：BOOT.BIN位于0，最大12MiB；压缩FIT位于0x00c00000，最大20MiB。RAM测试版本只从暂存DDR读取启动镜像，不能替代量产启动文件。打包器检查容量、重叠和散列，MCS工具独立解码检查每条记录校验和及与BIN的一致性。

## 使用和验证

PotPlayer选择本平台的UVC设备、YUY2、1280×1024、120fps。显示器刷新率和USB协商帧率分别验证；显示器120Hz或USB枚举成功，都不能单独证明每秒收到120张不同摄像头图像。

服务每秒记录采集、USB提交/完成、无效帧、丢弃候选、USB错误和DP切换计数。评估持续性能时使用稳定窗口的计数增量，而不是启动后的累计平均值。USB关闭时提交计数停止，但采集和DP计数应继续增长。

重新构建使用Vivado/Vitis2025.1与WSL AArch64交叉编译器。先构建PL并检查时序/DRC，再生成匹配XSA的固件，构建Linux与应用，打包并RAM验证，最后才替换QSPI。已有可恢复DP120工程保留在相邻 `sc130_dp_test/camera`，不要将新旧XSA、FSBL或bitstream混搭。

2026-10-21实板DMA目标地址修正及验证：[完整记录](reports/dma_address_fix_validation.md)。原内核64帧中44帧未覆盖预期缓冲，修正后64帧全部通过；当前QSPI状态以记录为准。


2026-10-21当前固化版本：[black52发行目录](dist/black52/README.md)。实际接入sc130_tonemap_yuy2，RAW黑位52校正，AEC自动、AGC手动，含VDMA地址修正及无DP时非阻塞查询。已烧录并验证QSPI复位启动；黑场Y均值55.2→17.0，用户确认灰雾减轻。USB当前约119～120fps但仍有少量错误，DP连接状态在最终复测时为disconnected；详细边界和证据见[验证报告](reports/black52_isp_validation.md)。


2026-10-22最新固化版本：[black52-hpd](dist/black52-hpd/README.md)，替代上一版black52。修正DP通知/轮询在连接器创建前初始化的问题，并增加无连接时的快速检测和后台补查。已烧录并验证从QSPI复位后自动连接DP、输出约120Hz，无手动detect。用户确认显示器一直连接；此前“未连接”是软件缓存状态，见[修复证据](reports/dp_hpd_boot_recovery.md)。


2026-10-22 USB复制性能实测：已加入默认关闭的分段统计，完成90秒A/B/A复核。CPU复制平均约2.15ms，约占单核26%，免重复复制降低应用CPU但未提高稳态吞吐；当前保留正常复制方案。实测报告：[USB性能与方案判断](reports/usb_copy_performance_20261022.md)。
