# 正常 DP120 与集成平台逐项对比

日期：2026-10-21。用户反馈：旧的仅 DP 程序没有重影；集成版 DP 与 USB 均有历史轮廓。
范围：离线源代码、保存的 BD、已有单元测试；不接硬件、不修改生产程序、不烧录。

## 基准及证据边界

基准是 `sc130_dp_test/camera` 的单次 PL 加载、DP120 双通道裸机版本。
依据 camera/README.md 和 qspi/README.md 的正常图像确认、QSPI 验证记录，
主应用是 src/dp_dual.c + src/dp_camera.c，不是历史 DP60 或双次 PL 归档。
集成版本是 `sc130_platform`：Linux V4L2采集、DRM DP、UVC复制输出、静态LUT。

`tools/compare_dp_baseline.py` 生成 `reports/dp_baseline_comparison.json`，
记录两侧源码 SHA256、保存 BD 的参数与连接对比，可复跑。
源码一致不等于重新证明历史烧录 bitstream 的逐字节身份；本次不重新构建或接板。

## 对比结果

| 项目 | 正常 DP 裸机版本 | 集成 Linux 版本 | 结论 |
|---|---|---|---|
| 摄像头配置表、I²C驱动、复位控制C | 原始实现 | 三个文件逐字节相同 | 未发现这些源文件的新增差异；执行时机由应用变为FSBL |
| CSI、复位、帧门控、双通道分配、AXI寄存器、write guard等 | 14个RTL文件 | 14个逐字节相同 | 两路奇偶帧采集本来就存在，不是本次新增 |
| VDMA0/1硬件参数 | 128-bit、4帧槽、单帧计数等 | 保存BD所有显式参数相同 | 未发现VDMA IP配置差异 |
| AXI interconnect和PL地址 | 原版BD | xbar参数、interface_nets、nets、addressing相同 | 未发现PL总线接线或地址空间差异 |
| PS配置 | DP启用、USB关闭 | USB0/USB3及对应MIO/GTR启用 | 参数差异集中USB和生成的保护/引脚属性；BD的DDR参数无差异 |
| ISP像素转换 | 线性RAW10→YUY2 | 静态1024项tone LUT | 新模块无历史帧存储；无时域混合 |
| 传输格式 | YUY2，1280×1024，stride2560 | 相同 | 整帧2621440字节 |
| CPU缓存 | main明确关闭D/I cache | cacheable non-coherent MMAP + 显式DMA同步 | 重要新增机制，裸机成功不能验证Linux一致性 |
| 帧内存分配 | 8个固定物理地址，0x21000000起，4MiB间隔 | VB2/CMA分配，常见实际9个，逐master映射 | 地址来源/映射完全改变 |
| VDMA帧地址寄存器 | 每次将所有FrameStoreStartAddr写为当前buffer地址 | Linux每次只更新desc_submitcount对应的一个槽，模4轮转 | 最值得做地址级验证的差异之一；不是已确认错误 |
| 完成判定 | gate完整结果+HALTED+guard字节数 | 同样检查，另加DMA callback/代次、HALTED后再取样 | 未发现集成版放宽完整性条件 |
| 显示完成与回收 | 独立DPDMA描述符DONE写回和序号水位 | DRM page-flip事件将pending→active，归还旧active | 软件状态检查通过；实际事件与硬件退休对应需实测 |
| USB | 无 | capture→独立UVC buffer全帧同步memcpy | 不直接共享UVC正在发送的buffer；增加CPU/DDR负载 |
| 播放顺序 | READY按最旧序号取出 | 内核有序发布，用户态DP/USB取最新有效帧 | 集成版会丢弃过期帧，不会主动混合像素 |

18个比对文件对全部字节相同：14个RTL、3个传感器/复位C文件，以及旧线性转换器
与集成工程reference目录备份。**实际参与综合的转换器在isp目录，已经改变**，不能把18/18
误读为整个工程功能未变。

## 关键差异一：硬件帧槽与软件buffer编号

旧版 src/dp_dual.c 的 arm_channel 遍历 dma[ch].MaxNumFrames，
将每个 FrameStoreStartAddr 都设置为 buffers[slot].Address。
所以无论硬件取哪一个地址槽，该次目的地址均是当前软件buffer。

集成 linux/kernel/sc130_capture.c 使用当前buffer的DMA地址构造一个interleaved descriptor。
实际Linux xilinx_vdma_start_transfer 只写 desc_submitcount 对应 START_ADDRESS，
随后软件计数模 num_frms(4)轮转。硬件仍运行 circular+单帧停止。
此模式依赖硬件当前帧槽与驱动计数一致；仅从调用代码不能证明运行时是否失步。

如果硬件错误地取到旧地址槽，可能出现“当前buffer被发布，但新数据写入另一buffer”，
甚至覆盖仍在显示的buffer。现有write guard不观察AWADDR，不能拦截这一类错误。
**这是可解释症状的假设，不是故障已发生的证据，也不能据此直接修改通用驱动。**
验证应同时记录当前buffer的DMA base、4个START_ADDRESS、PARK_PTR当前写槽、
以及实际AWADDR。若一致，此假设应降级，不继续围绕它盲改。

## 关键差异二：缓存和映射

旧版 dp_camera.c main调用 Xil_DCacheDisable/Xil_ICacheDisable，避免CPU脏行问题。
Linux版 platform_bridge.c 申请 V4L2_MEMORY_FLAG_NON_COHERENT，缓存MMAP供CPU复制；
capture驱动按实际master写前/写后做dma_sync_sgtable，VB2 prepare/finish也同步。
DT未将HP0标为dma-coherent。

未发现简单的“漏掉全部cache同步”错误；但页别名、方向、设备属性和实际AXI行为
不能由单元测试覆盖。应以相同Linux采集路径下cacheable/默认coherent分配的
单变量A/B诊断验证，不在Linux中直接全局关闭cache，也不把降低帧率当成修复。

## 帧归属检查

内核 dual_begin 只认D_QUEUED，拿走后立即变D_CH0/D_CH1，另一通道不能重复拿走。
用户态broker只允许非queued、非latest、非DP-held的buffer重新QBUF。
DP提交前hold，失败撤销本次hold；事件归还旧active，新active继续hold。
USB memcpy在单线程主循环完成，latest在复制期间不归还；USB输出使用另一组buffer。
代码中没有发现明显的“USB停止就释放DP帧”或“刚提交DP立即QBUF”路径。

原版直接看DPDMA DONE，Linux依赖内核DRM事件。当前ownership测试模拟事件，
能验证应用状态机，不能证明事件确实晚于最后一次硬件读。
因此两者的完成语义差异仍列入硬件检查，但不是已确认提前释放。

## ISP与显示

原线性映射和新tone LUT都是单级流水。新模块在无效拍将SOF/EOL清零，
原版无效拍可能保留marker；下游都以valid限定，所以未发现因此混入旧帧的路径。
新LUT没有递归累加或时域缓存；它可能提高已有弱轮廓的可见性，
但当前没有证据证明所见轮廓在输入RAW里已存在。

DP显示从裸机DPDMA描述符改为DRM的YUYV video plane+透明graphics plane。
alpha是全局图层属性，摄像头YUYV帧没有逐像素alpha。
正常路径中没有把上一摄像头帧作为另一图层叠加的应用代码。
USB输出也是完整复制，不能仅凭“半透明样重影”就断言程序做了多帧融合。

## 验证与优先级

重跑原版frame_pool测试、集成kernel dual纯状态测试和software的5项测试均通过。
日志：reports/dp_baseline_comparison_tests.log。此前write guard六项XSim见
reports/dma_completeness_audit.md；地址反例只是监视能力反例，不是真实VDMA IP仿真。

优先级：

1. 同一Linux/ISP下追踪VDMA实际写地址是否等于本次buffer，检查四槽轮转。
2. 相同条件下验证cacheable与默认分配的数据一致性，并做确定性全帧图案比对。
3. 追踪DRM事件、DPDMA切换和QBUF之间的时序，确认没有提前复用。
4. ISP线性旁路、USB不发送的单变量对照。单纯关闭PotPlayer并不会撤销DRM和cache变化。

结论：对比把范围收敛到Linux软件管理及数据可见性变化；
VDMA多地址槽行为是新增的具体排查点。尚无足够证据认定重影根因或交付修复。

更新：2026-10-21已验证全部VDMA地址槽写同一当前目标的基准行为能够修复未覆盖缓冲问题，见 [实板证据](dma_address_fix_validation.md)。
