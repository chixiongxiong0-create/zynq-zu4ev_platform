# DMA目标地址错位：实板复现与修正

日期：2026-10-21。测试对象：SC130 ISP + DP120 + USB3 双输出平台。

## 已确认的故障

Linux VDMA 驱动每次提交单帧时轮换更新一个硬件帧地址槽。当前停止/重启采集方式下，硬件实际使用的槽与软件预期目标不一致。完成中断和完整 bytesused 不能证明预期缓冲区已经被覆盖，旧画面因而可被当作新帧发布。这与用户描述的数秒前轮廓残留一致；视觉症状最终仍须由显示端复核。

正常的独立 DP 程序每次 arm 都将当前目标地址写入全部硬件地址槽。0004 内核补丁将本平台单帧事务恢复为该做法；未更改 ISP、PL 或生产采集驱动代码。

## 覆盖检查

临时诊断模块在每路前32帧提交前，把整块2,621,440字节目标缓冲填为0x5A；完成后逐个32位字检查固定UV字节是否已被真实YUY2覆盖。检查不依赖图像是否运动，也不依赖亮度内容。串口和dmesg重复行按通道及帧号去重。

| 条件 | 独立帧数 | 未完整覆盖 |
|---|---:|---:|
| 原内核 | 64 | 44（通道0：20，通道1：24） |
| 0004修正内核 | 64 | 0 |

全部64帧在原内核中也均报告完整payload。冻结采集后的JTAG物理DDR读回，确认第二帧预期目标的首尾仍为0x5A，排除了“只有CPU看见旧缓存”的解释。仅设置park=1的试验仍失败，未采用。

原始记录：dma_audit_full.log、dma_audit_frozen.log、dma_audit_ddr.log、dma_audit_park_start.log、dma_audit_after.log。去重汇总：dma_audit_before.json、dma_audit_after.json。诊断代码生成器：../tools/prepare_dma_audit.py。最终运行恢复生产模块，不包含填充或覆盖扫描。

## 正常双输出验证

修正内核恢复正常摄像头后，DP切换计数持续约120次/秒、采集invalid=0。电脑DirectShow/FFmpeg收到1800帧，约15.02秒，120fps（dma_fixed_usb120.log）。这验证了传输吞吐，不单独证明长期无重复图像。首次USB STREAMON后累计usb_error出现1，此后测试期间未继续增加；不能据此宣称USB错误为零。

当前修正通过RAM启动验证，QSPI仍保留原独立DP程序。显示端历史重影的人工复核尚待确认。

## 冷启动另一个问题

原FSBL在psu_init前调用usleep，实板停在尚处复位的系统计数器访问；同时对只写ENABLE_SET寄存器做了读改写。sc130_power.c改为直接写SET/TRIGGER并使用有界状态轮询，不依赖初始化前的计时器。轮询上限是次数，不是标定的毫秒数。单元测试覆盖只读状态访问、写入顺序、已上电快路径、延后成功和超时。新的FSBL已编译并通过OCM布局检查，独立启动验证另行记录。

## 修正FSBL重启复测

新RAM FSBL自行完成USB电源请求并到达fffc5728启动断点；本轮使用原tools/ram_boot.tcl，没有JTAG预上电步骤。一次PL加载后正常进入Linux和双输出服务。日志：dma_fixed_fsbl_ram_boot.log、dma_fixed_fsbl_uart.log。此为系统复位后的RAM启动验证，不等同于断电QSPI独立启动验证。

重新启动后电脑接收3599帧/30.02秒，报告120fps，见dma_fixed_reboot_usb120.log。采集invalid仍为0，DP稳定窗口约120次切换/秒。USB完成缓冲错误计数在测试中由1增加到2，结束后累计3（dma_fixed_reboot_live.log、dma_fixed_reboot_final.log）；因此USB仍有少量错误需要后续追踪，未将本轮标记为零错误验收。

新候选文件在dist/dma-fix，包含修正内核及FSBL；QSPI_LINUX.mcs独立解码校验通过，二进制25,930,700字节。散列与尺寸见dma_fixed_manifest.json。尚未烧录，QSPI保留独立DP旧版本；板上当前运行修正版RAM程序。需要显示端确认历史重影是否消失。

用户已确认：DP与PotPlayer历史重影均消失。随后静止现象另见 [现场检查](freeze_scene_analysis.md)，不与之前的DMA旧帧故障混为一项。

后续状态更新：black52版本已将黑位校正、AEC默认值及DMA修正固化QSPI，详见[最新验证报告](black52_isp_validation.md)。本文件此前“尚未烧录/未修改ISP”等描述为当时状态。
