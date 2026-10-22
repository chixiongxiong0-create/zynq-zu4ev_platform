# SC130 black52-hpd：当前QSPI发行版本

2026-10-22，替代上一版black52。FPGA/ISP与其相同：实际sc130_tonemap_yuy2、RAW10黑位52、输出YUY2；sensor自动曝光和自动BLC/RNC开启，自动增益保持手动。

内核新增0005：在连接器创建后初始化HPD/轮询；未连接时快速返回；后台补查延迟或遗漏的连接通知。用户态仍使用不强制探测的缓存查询。修复已通过驱动函数测试、构建、RAM启动和QSPI复位启动验证，无手动detect，DP/采集持续约120fps，USB枚举SuperSpeed。

QSPI_LINUX.mcs为Intel HEX，QSPI_LINUX.bin为25,930,076字节原始镜像。MCS独立解码一致，实际Flash逐字节与完整32MiB SHA256校验均通过；manifest.json记录配置和校验。板上当前运行本版本。

USB仍有既有完成错误，RAM复测约117fps，尚未通过零错误120fps验收。尚未进行人工断电冷启动及本轮拔插循环测试。原始独立DP备份保留在../../backups/qspi_before_platform.bin。

证据：../../reports/dp_hpd_boot_recovery.md。电脑采集测试已释放设备，板卡重启后请重新打开PotPlayer设备会话。
