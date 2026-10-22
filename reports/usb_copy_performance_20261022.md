# USB CPU复制性能实测与方案决策

日期：2026-10-22。第一步完成；不将CPU复制判定为当前120fps稳态吞吐的主要瓶颈，因此未触发DMA-BUF直通改造。复制仍是应用最大的CPU开销，减少复制有节省CPU和约2ms路径耗时的潜在价值；本结论不是“复制没有成本”，也不是“复制对错误概率绝无影响”。

## 测试条件

当前实板QSPI Linux 6.12.0 #8、PL/ISP黑位52、DP 1280x1024@120Hz与摄像头约119.966fps保持不变。PS USB0枚举SuperSpeed。仅RAM替换应用，不改内核、USB描述符、IRQ配置、缓存同步、传感器、PL或Flash。已有USB IRQ亲和性为CPU3；应用允许CPU0..3，不新增实时调度策略。测试关闭PotPlayer以保证主机唯一采集者，使用FFmpeg7.1 DirectShow、YUY2、1280x1024、120fps、512MiB rtbufsize，输出null，不执行显示渲染/编码。

正常复制A → 冻结对照B → 正常复制A2，每组主机运行90秒并自然退出后才停止板端应用。B仍运行真实摄像头和DP，只让每个UVC输出槽第一次复制后反复发送静态内容，不能用于宣称真实新帧率。三组使用同一个诊断程序，板端SHA256 bedfd877caef0ee25d05f2e4110efec42a7e26d22a20575227abfda937f10ead。

## 匹配窗口结果

| 指标 | A真实复制 | B免重复复制、冻结USB图像 | A2真实复制 |
|---|---:|---:|---:|
| 主机收到帧数 | 10794 | 10798 | 10775 |
| FFmpeg报告fps | 119.87 | 119.93 | 119.65 |
| 应用CPU，单核100% | 37.841% | 10.362% | 37.938% |
| memcpy平均墙钟 | 2.151ms | 仅初始化4次 | 2.167ms |
| memcpy最大墙钟 | 3.920ms | 不作稳态比较 | 4.005ms |
| memcpy CPU占单核 | 25.822% | 0.017% | 25.859% |
| 平均提交间隔 | 8.335456ms | 8.335220ms | 8.335213ms |
| QBUF平均调用 | 0.431ms | 0.402ms | 0.432ms |
| 取帧到提交平均 | 2.731ms | 0.531ms | 3.009ms |
| QBUF调用到DQBUF平均 | 8.849ms | 8.756ms | 8.844ms |
| 稳态USB ERROR缓冲 | 5 | 1 | 17 |
| capture invalid | 0 | 0 | 0 |
| 归属/DQBUF/事件/发送错误 | 全0 | 全0 | 全0 |
| 采集与DP稳态fps | 119.966 | 119.966 | 119.966 |

稳态窗口按板端stats取约89.025秒增量；USB完成含ERROR归还，不能等同于电脑成功收到完整帧。主机与板端起止不同，不能直接用sent减主机frame推算错误数。Host fps来自FFmpeg真实运行统计，media_seconds来自媒体时间戳，两种时间不混用。

复制占应用CPU约68%，但应用没有占满一个核心；去掉复制后稳态提交/归还速率没有上升，仍受120fps源与输出节奏约束。2.15ms复制成本不是眼下吞吐被CPU卡住的证据。当前支持“CPU资源优化有价值”，不支持“去掉复制即可解决USB120吞吐或ERROR”。

错误数存在波动：A为5、A2为17、B为1。此前探索性的免复制90秒也出现14个ERROR，说明免复制不能消除该错误，但这些不同窗口不足以严格证明复制对错误概率完全没有贡献。此前独立诊断见usb_board_20261022.md：已捕获DWC3 missed-isoc/-EXDEV，仍需针对USB request调度、IRQ和主机侧条件继续研究。

## 分布与计时边界

copy_wall的P99桶上界为4.096ms，真实采样最大值见表。P50/P95/P99是log2微秒桶上界，不把4.096ms当作精确分位数。copy_cpu与copy_wall计时边界相差少量计时调用开销，某些平均CPU值略大于墙钟值，不解释为CPU超出实际时间。

capture_complete_to_dequeue使用驱动软件完成的V4L2时间戳，绝不是曝光或PL SOF；当前约0.73ms。QBUF到DQBUF包含内核排队、USB传输和用户态调度，不是纯USB传输时长，更不是glass-to-glass。实测版本该采集延迟统计在停USB后还记录了空闲采集样本，汇总脚本选择最大sent时最早完整统计块来减少污染；最终源码已限制为USB streaming且计时已开始时记录，其他复制/提交/CPU测量逻辑不变。

初始实时探索窗口因人为提前停应用使主机I/O退出，不用于匹配吞吐比较；最初启动重开流曾因缺少新CONNECT出现控制事件错误，经逻辑断连/重连后正式A/B/A2均无此类错误。初始探索脚本统计进程全生命周期，含等待主机的空闲时间；正式版本改为本次流活动时间，避免CPU占用被空闲稀释。所有边界异常保留原日志，不隐藏。

## 第一阶段改动与检查

- 新增默认关闭的--profile及固定内存直方图，逐帧只计时/计数，每秒汇总；正常启动不输出PERF。
- 新增明确标为冻结USB图像的--profile-static-usb诊断对照，不作为交付视频模式。
- 保持正常memcpy、源缓冲保护、DP active/pending归属和USB独立输出槽不变，不跳过cache sync。
- 保留已有按类统计和采样USB错误日志，避免把ERROR归还当成协议失败或成功图像。
- 检查交叉glibc提供AArch64 memcpy IFUNC及含128位q寄存器读写的generic实现；没有证据表明当前在执行自制逐字节复制，因此未盲目替换汇编。该检查不等同于运行时逐指令trace。
- 现有IRQ已绑定CPU3，未凭猜测提高优先级、增加缓冲或固定应用CPU；也没有用扩大队列掩盖延迟。
- 8个测试程序全部通过：新统计/复制对照测试，以及broker、DP归属、平台事件、快照、USB序号标记和完成错误分类。AArch64静态程序构建成功。

## DMA-BUF判断

本地Linux源码uvc_queue.c声明VB2_DMABUF，gadget.sg_supported时选择vb2_dma_sg_memops；uvc_video.c存在uvc_video_encode_isoc_sg，USB request的SG引用图像页并另放UVC头。因此源码具备必要支持，后续可研究直接导入采集DMA-BUF。

但本次条件“CPU复制已证实为主要吞吐瓶颈”未成立，未修改捕获缓冲共享协议，也未将源码支持冒称为端到端直通验证。直通仍需验证实际导入/缓存同步、USB与DP同时持有、停流取消后的引用释放，以及USB长期占帧不会拖住采集。当前保留已验证的复制路径。

## 可复查证据

- perf_A_board.log、perf_B_board.log、perf_A2_board.log：板端完整应用日志，经gzip/base64串口下载并严格解码。
- perf_A90_host.progress、perf_B90_host.progress、perf_A2_90_host.progress及对应log：主机收帧。
- usb_copy_performance_summary.json：统一机器可读汇总。
- tools/analyze_usb_perf.py：重建汇总，命令python tools/analyze_usb_perf.py reports。
- perf_final_tests.log、perf_final_build.log：最终离线测试和静态构建。

QSPI内容未改写。测试结束已恢复/usr/bin/sc130_platform_bridge原应用，SHA256 0d6e818e45b426476e87944e277104d03030d73cd99cb8d2971d4ce8d3558fe1；DP connected、USB super-speed、采集/DP继续约120fps、invalid=0。没有保留冻结输出作为运行程序。恢复后的USB短测另见perf_restored_host日志。

恢复后15秒主机短测收到1801帧，正常退出；板端invalid=0、USB错误总数0、DP/采集继续约120fps（perf_restored_verified.log）。这是恢复检查，不抵消之前长窗口的ERROR，也不宣称USB错误已修复。最终新增计时源码不编入现有QSPI，保持已验证启动镜像身份。
