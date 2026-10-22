# DMA 整帧写入离线检查（2026-10-21）

## 结论

尚未定位重影根因。当前代码没有发现明确的 HSIZE/VSIZE/stride 不一致，
也不是仅凭帧序号或中断就发布成功帧。现有完成检查能排除多种短写与响应错误，
但不能证明写地址覆盖了目标缓冲区的每个字节。

本次没有接入硬件，没有更改生产 RTL、驱动、启动镜像或 QSPI。
新增文件仅为测试平台、运行脚本和本报告。

## 已核对的数据路径

1. `linux/kernel/sc130_capture.c`：W=1280、H=1024、STRIDE=2560，SIZE=2621440。
   interleaved descriptor 的 numf=1024、chunk.size=2560、icg=0、frame_size=1，
   dst_start 使用当前 buffer 对应 DMA master 的映射地址，不是用户态虚拟地址。
2. 实际 Linux 源码 `drivers/dma/xilinx/xilinx_dma.c` 的
   xilinx_vdma_dma_prep_interleaved 将 numf→VSIZE、size→HSIZE、size+icg→stride，
   dst_start→buf_addr；start_transfer 将对应参数写入硬件。
   软件描述符对应连续 2621440 字节，无显式行间空洞。
3. Vivado replay `tools/recreate_project.tcl`：两路 VDMA 均为 128-bit 内存主口、
   32-bit 地址、4 个硬件 frame store、16-beat 最大 burst、S2MM frame counter 开启。
   DT 同为 4 个 frame store；Linux 设置单帧计数和 coalesc=1。
   硬件的 4 个地址寄存器槽与用户态的 9 个采集 buffer 是不同概念。
   实际 frame store 索引是否始终对应预期地址，仍需硬件寄存器或 AXI 地址轨迹证明。
4. prepare_buffer 检查每路映射的 DMA 地址段连续、容量不少于 SIZE、地址不越过32位。
   两路分配通过 dual_begin 的 D_QUEUED→D_CH0/D_CH1 状态转换互斥。
5. 成功帧必须同时满足：DMA callback、有效结果、epoch 匹配、无超时、flags=1、
   words=655360、lines=1024、通道/帧号奇偶匹配、guard=5、bytes=2621440、VDMA halted。
   HALTED 后再读一次 guard/bytes/flags；失败帧按 ERROR 返回，不作为正常 payload 发布。
6. capture_frame_gate 对每行 640 个32-bit字和1024行进行计数；短行/长行、
   FIFO 溢出、源错误、DMA 错误会失败。帧关闭还需要下一 SOF，
   lookahead beat 用于完成单帧 VDMA 关闭协议；本次没有重新仿真真实 VDMA IP。
7. 写前和写后分别调用对应 DMA master 的 dma_sync_sgtable_for_device/for_cpu。
   VB2 non-coherent MMAP 自身也有 prepare/finish 同步；用户态未申请跳过同步。
   DT 未将 HP0 VDMA 错标为 dma-coherent。这是静态调用链检查，不能证明运行时所有
   cache、页映射和硬件行为正确。
8. USB 按 SIZE 完整 memcpy；DP framebuffer pitch 为2560。此处未发现局部复制配置。

## 对生产 write guard 的 XSim 实测

测试：`sim/dma/tb_write_completeness.sv`，运行：`sim/dma/run.ps1`。
使用原始 `rtl/vdma_write_guard_v2.v`，没有修改 DUT。
完整日志：`sim/dma/run.log`。仿真正常结束，六项断言测试通过。

| 注入情况 | 结果 |
|---|---|
| 完整连续帧，2621440字节 | retired，符合预期 |
| 少一个128-bit数据拍 | 不 retired |
| 某拍 WSTRB 仅部分字节有效 | error，不 retired |
| BRESP=SLVERR | error，不 retired |
| 最后一笔写响应延迟 | 等待响应，收到后才 retired |
| 写入总量完整，但外部地址模型反复覆盖第一行 | 仍 retired；仅2560字节唯一覆盖 |

最后一项是**检查能力反例**，不是实板故障复现：DUT 根本没有 AWADDR/AWLEN/AWSIZE
输入，所以相同 AW/W/B 握手下，正确地址和重复地址对 DUT 不可区分。
测试平台单独计算假设地址的唯一覆盖范围，不模拟真实 VDMA 的地址生成。
因此以前的 guard 正常/bytes 正常/帧序号递增不能单独排除地址错误。

## 接入硬件后的最小验证

优先捕获每通道 AWADDR/AWLEN/AWSIZE/AWBURST 和实际 buffer DMA base，
核对每个 burst 的地址、长度、边界和整个帧的唯一字节覆盖；结合 WSTRB、WLAST、BRESP。
完整帧的预期地址区间为 [base, base+2621440)，各行起点 base+y*2560。
两路各有独立 base，不能互相混用。

另备独立诊断模式：QBUF 前用与该 buffer/代次相关的哨兵图案填充全帧，
完成缓存交接后采集，D QBUF 后检查是否残留哨兵；该模式开销很大，
应使用低负载验证，不能拿它证明120fps性能。更强的端到端验证使用
由(frame,x,y)生成的确定性测试图并逐像素比对，可区分地址覆盖和数据陈旧。

这些诊断尚未加入生产逻辑。只有硬件轨迹或端到端数据比对才能确认实板是否存在
错地址、部分覆盖或旧数据；当前不应据此改 stride、清零所有正常帧或宣布重影已修复。

更新：2026-10-21已在实板确认错误目标地址导致预期帧缓存未被写入，见 [修正验证](dma_address_fix_validation.md)。
