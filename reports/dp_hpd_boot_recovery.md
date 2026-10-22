# DP QSPI启动连接状态恢复（2026-10-22）

用户确认Mini DP线一直连接、显示器一直开机并选择DP输入。不能将先前DRM disconnected状态解释为用户拔线。

## 现场与恢复试验

原QSPI内核运行约8小时后，DP硬件INTERRUPT_SIGNAL_STATE(fd4a0130)=0x5，HPD bit0为1，但/sys/class/drm/card0-DP-1/status仍disconnected，应用持续DP open failed且dp_flips=0。采集持续120fps，未整体死机。日志dp_qspi_hpd_initial.log。

在不重启、不重新配置PL、不改动视频程序的情况下，向DRM status写入detect（让驱动真实检测，而非强制on），状态立即变connected，DP翻页恢复约120次/秒（dp_qspi_reprobe.log）。因此物理显示器实际可通信，原连接状态没有自动刷新。

## 源码根因

Linux zynqmp_kms.c在zynqmp_dpsub_kms_init创建连接器之前调用drm_kms_helper_poll_init。该初始化会遍历当时已存在的连接器，为它们挂HPD通知并决定是否排队后台轮询；当时没有连接器。后续kms_init创建连接器，但初始化不会自动重跑。原先用户态count_modes=0强制查询掩盖该问题，也在无DP时同步等待一秒。上一轮改缓存查询后暴露了未自动更新的状态。

## 修正（0005补丁）

1. 在kms_init成功创建连接器之后再初始化HPD/轮询；初始化失败直接返回，不清理尚未初始化的poll。
2. 该连接器使用POLL_CONNECT，后台每约10秒补查断开状态；正常桥HPD通知仍开启，已连接视频不做周期探测。
3. zynqmp_dp的HPD低电平检测立即返回，不做原有10×100ms等待。延迟HPD由后台连接检测处理，避免持有DRM互斥锁阻塞共享采集/USB主循环。

补丁linux/patches/0005-dp-fast-detect-and-connect-poll.patch，构建自动运行linux/tests/test_dp_hpd_poll.py。测试抽取真实驱动detect函数，用HPD/AUX桩执行低电平无睡眠、正常能力限幅、AUX失败；另校验连接轮询配置及初始化顺序。旧函数和旧初始化顺序分别先失败（dp_hpd_poll_red.log、dp_hpd_init_order_red.log），修正版通过三个PASS，内核和模块构建成功（dp_hpd_order_kernel_build.log）。

只更新内核；不改ISP曲线、黑位52、曝光配置、PL或PS参数。用户态保留已验证的不强制探测查询。新发行目录dist/black52-hpd；RAM验证与固化结果在实际完成后补充。


## 最终修正版RAM验证

加入初始化顺序修正的最终0005镜像已RAM启动，不执行手动detect。DRM自动connected、HPD=1、poll=Y，DP/采集稳定窗口每秒各增加120，invalid=0（dp_hpd_fixed_ram_live.log）。内核版本构建号#8，Oct22 07:39:05，与最终新Image构建一致。注意之前dp_hpd_ram_boot.log是排查过程中未包含顺序修正的中间候选，不能代替最终验证。

电脑USB20.02秒收到2348帧（约117fps，dp_hpd_fixed_ram_usb120.log），仍有完成错误；与DP启动恢复分开记录，不宣称USB零丢帧验收。用户此前已确认一次手动重检测后DP画面正常，最终修正版启动后的DP输出另由计数与连接状态验证。

固化前读取到原QSPI全片SHA256=6f050507848a8e3d8cb02e9eaecbdaddade25b36b8eb895ad49ea7e84bb58671，与上版记录一致。新镜像二进制25,930,076字节，MCS独立解码通过。程序/黑位/PL保持上版，新增内核0005补丁。最终Flash写入及复位启动结果随后补充。


## 最终固化和QSPI启动

最终Flash逐字节校验成功，32MiB全片SHA256=45a9bc2783323e0d9c633acdaf3c6b55599e2a17a4aa909371df003fb89236a1，与包含未擦写尾区的预期一致（dp_hpd_flash_write_verify.log）。清除BOOT_MODE覆盖后系统复位，仅解除调试器reset catch继续BootROM；没有JTAG下载FSBL/PL/Linux。UART确认单次PL加载、传感器就绪、U-Boot从QSPI offset0xc00000读取FIT并进入新内核#8。

未执行手动detect，QSPI启动后DRM自动connected、HPD=1，约5秒时已开始DP翻页；稳定窗口DP和采集每秒约增加120，invalid=0，USB重新枚举SuperSpeed（dp_hpd_qspi_live.log）。当前板上运行最终QSPI版本。没有进行人工断电冷启动或本轮拔插循环验收；USB传输仍有既有错误，未归入本次DP修复完成范围。

最新发行：dist/black52-hpd/QSPI_LINUX.mcs。原始独立DP全片备份仍保留backups/qspi_before_platform.bin，上版black52也保留作为历史对照。不能再将上一版的disconnected表述为物理未接线：本次用户明确确认一直连接，现场和源码已证实为状态更新缺失。
