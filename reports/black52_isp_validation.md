# sc130_tonemap_yuy2 + RAW黑位52校正

用户要求：实际ISP流程使用附件sc130_tonemap_yuy2，并按已测遮光黑位加入校正。

## 实现

实际模块路径：rtl/uvc_capture_dual.v 的 converter → isp/raw10_to_yuy2.v 包装 → isp/sc130_tonemap_yuy2.v 的 tone_map。附件vendor中的4个原件散列保持不变，新模块保留附件的两像素并行、Y0/U/Y1/V打包及一级查表流水，增加clock enable以保留包装器暂停保持行为。构建、重建和仿真脚本均显式包含新模块，避免只放文件但没有接入。

黑位52来自reports/black_level_calibration.md的遮光测试；原Y中位55对应RAW10约52..53。本版取52，公式为 index=round_half_up(max(raw-52,0)*1023/971)，Y=附件原始LUT[index]。黑位扣除与归一化在生成时折入1024项ROM；无运行时除法、无额外流水级。RAW0..52→Y16，RAW1023→Y235。包装器BYPASS=1仍为原线性未校正路径，便于A/B比较；正常构建为0。

生成入口isp/generate_rtl.py --black 52，参数及散列写入isp/tone_config.json；可从任意工作目录运行。ROM在RTL内初始化，bitstream不依赖运行时mem路径。sensor自动BLC/RNC保持打开，FSBL源码AEC=自动、AGC=手动。

## 离线验证

新测试在旧RTL下66ns明确失败（reports/isp/black_red_run.log）；新版XSim通过3424次检查及8拍暂停，覆盖全部1024码双lane与交换顺序、黑白端点、旁路、空拍、帧行标记、持续数据、复位和暂停中复位，保留既有故障标记单级延迟检查模型。日志reports/isp/green_run.log、green_compile.log、green_elaborate.log。单元仿真不等同于整机复位及故障全覆盖。

ROM生成单独核对1024项单调性、合法亮度范围及黑白端点；附件4原件SHA256全部匹配。软件与FSBL准备回归通过（black52_software_regression.log），生产/RAM FSBL均编译并通过OCM布局检查（black52_fsbl_build.log、black52_ram_fsbl_build.log）。Vivado MCP启动失败，使用工程固定2025.1 CLI构建；结果在下方按实际完成情况补充。

此前可运行bit/XSA/LTX保存在build/isp-pre-black/。后续实际结果如下。

## 实现及RAM实板验证

Vivado完成综合、布局布线和bitstream，PLATFORM_BUILD_PASS；最终setup/hold裕量0.396/0.010ns，DRC错误0，独立检查PLATFORM_INSPECTION_PASS。综合日志确认实际sc130_tonemap_yuy2模块及converter/isp.tone_map双口ROM。导出XSA内6个psu_init文件与上版逐字节相同，未改变PS配置，可复用匹配BSP。

单次PL RAM启动成功，FSBL配置AEC自动，实读3e03=0a，曝光20d0，增益02/20；5001=45、3902=45、3400=53，BLC和RNC自动模式保持开启。

用户确认保持遮光时采到第一帧：1,310,720像素，平均Y17.0337，中位16，1%/99%分位16/21，约50.8%像素在Y16。此前同工作点三帧遮光均值约55.2、中位55。此为实际板上输出，证明黑位底座被校正到接近视频黑色。

随后两帧均值73.72/73.70，不计入黑场统计；用户明确确认已经移开遮挡，并反馈真实画面灰雾明显减轻。不能把这两帧误计为遮光回归。

DP及采集计数稳定窗口约120次/秒，invalid=0。USB DirectShow测试30.01秒收到3591帧、报告120fps（black52_usb120.log）；不是零丢帧/零错误验收。既有USB完成错误计数仍存在，后续独立追踪。本轮ISP功能通过不代表USB长期可靠性问题已解决。

新镜像在dist/black52，MCS解码校验通过，QSPI二进制25,930,700字节。QSPI固化及独立启动结果在完成后补充。

## QSPI固化与DP缺席阻塞修正

第一次固化黑位52镜像后，逐字节及整片SHA256均匹配（black52_flash_write_verify.log）；从Flash复位启动出现SINGLE_PL count=1、SENSOR_READY和Linux启动，无重新下载PL。此为QSPI系统复位启动验证，非人工断电冷启动。随后发现DP HPD实际为0、DRM状态disconnected（black52_hpd_check.log），显示器连接情况已向用户核实，尚待回复。

在此实际无DP环境，原程序每2秒执行count_modes=0的DRM GETCONNECTOR，Linux drm_connector.c明确将它解释为强制fill_modes；同步HPD等待阻塞共享采集/USB主循环，20秒只收到1108帧。不是ISP像素链路吞吐变化。

software/dp_display.c改为带非零模式容量和有效模式数组的缓存连接查询，第二次读也避免count_modes=0；依靠内核热插拔工作更新连接状态。现有dp_ownership_test增加无显示器场景，旧代码先断言失败（black52_dp_probe_red.log），修改后全部软件回归通过（black52_dp_probe_build.log）。实板新程序SHA256为0d6e818e45b426476e87944e277104d03030d73cd99cb8d2971d4ce8d3558fe1，与打包根文件系统逐字节相同。

无DP时修正后采集稳定约120fps，USB20.01秒收到2393帧（black52_dp_absent_fixed_usb.log），恢复约120fps；仍非零错误验收。DP物理连接未恢复，因此这项软件改动之后的DP接回显示尚待实测。之前黑位52硬件在DP正常连接时已经实测并获用户确认灰雾减轻。

最终发行镜像重新打包，含此修正，MCS对应二进制25,930,616字节，编码回读一致。第一次固化镜像已另存build/black52-before-dp-probe/作对照。最终QSPI结果以black52_final_flash_write_verify.log及后续启动记录为准。


最终固化完成：black52_final_flash_write_verify.log中逐字节校验通过，整片32MiB Flash SHA256=6f050507848a8e3d8cb02e9eaecbdaddade25b36b8eb895ad49ea7e84bb58671，与包含未擦写尾区的预期完全一致。原始独立DP完整32MiB备份仍保存在backups/qspi_before_platform.bin。

清除启动覆盖后再次从QSPI系统复位启动。black52_final_boot_uart.log确认SINGLE_PL count=1、SENSOR_READY，U-Boot从QSPI offset0xc00000读取FIT并启动Linux。只解除JTAG reset catch恢复BootROM运行，没有下载FSBL/PL/内核。/usr/bin/sc130_platform_bridge SHA256匹配最终修正版0d6e818e...；无DP时采集仍稳定约120fps、invalid=0。注意仍未进行人工断电冷启动测试；DP状态仍disconnected，未冒充已验证接回后的显示。


最终QSPI启动后USB接收20.02秒、2381帧（约119fps，black52_final_boot_usb120.log），板端采集稳定120fps、invalid=0。累计USB完成错误20，仍需单独调试，不能称为零错误120fps验收。最终传感器读回3902=45、3400=53、3e03=0a，确认自动BLC/RNC/AEC随镜像启动生效。电脑测试已结束并释放设备，可重新打开PotPlayer。板卡保持运行最终QSPI程序。

2026-10-22状态更正：用户确认DP显示器一直物理连接。已定位并修复驱动HPD/轮询初始化顺序，最终black52-hpd版本已固化并从QSPI自动恢复DP120输出，见[最新记录](dp_hpd_boot_recovery.md)。此前disconnected不能解释为用户拔线。
