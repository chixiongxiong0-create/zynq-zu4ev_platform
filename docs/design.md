# SC130 ISP 学习基础平台设计

用户目标：把已经实测的 DP120 和 USB3 UVC120 摄像头预览合成完整工程，加入附件 RAW10→YUY2 tone mapping 改善暗光，QSPI 自启动并实板验证。用户授权自主完成构建、烧录、验证，不等待中途评审。

## 选定架构

SC130双lane RAW10→既有独立复位CSI链路→附件固定亮度LUT/可选线性旁路→既有双相位VDMA→Linux V4L2缓存一致性正确的采集缓冲。PS DP使用DRM YUYV overlay导入相同DMA-BUF，USB使用已经实测的缓存MMAP复制到UVC输出。DP在没有USB主机时仍持续预览；USB主机打开/关闭不终止摄像头/DP。

DP占GTR3/2、ref2=27MHz；USB0占GTR1、ref1=26MHz，ULPI/MIO和原理图一致。1280×1024、源约119.966fps，DP120Hz，USB显式120fps。若显示器拒绝120模式可诊断报告，不能静默降到60后宣称120。

ISP为固定全局tone curve，黑位默认0、白锚1023，Y范围16..235、CbCr128，两个RAW10像素/200MHz拍、一个时钟延迟。保留来源文件、LUT生成和旁路参数，不声称降噪/自动曝光/恢复过曝细节。

## 缓冲区约束

每个采集缓冲最多由capture-ready、DP pending、DP active和USB复制这些使用者持有。USB输出独立内存，复制完成即解除USB对采集页的持有；只有不再被DP引用的页才能QBUF。DP仅在替换帧page-flip事件后释放旧active页；pending不可覆盖。USB慢时保留最新完整帧，不能阻塞相机造成半帧。USB断开只回收输出队列；DP继续。显示停止失败必须保留采集页，不回收到DMA。

## 启动与固化

新建独立sc130_platform，保留两个已验证工程和原QSPI恢复镜像。复用DP分支独立复位top，开启PS USB0并加入ISP。匹配新XSA生成FSBL/PMUFW。QSPI分成前12MiB BOOT.BIN和后20MiB FIT，压缩内核，含完整RAM rootfs，避免SD/网络/JTAG依赖。FSBL在PL加载完成后执行传感器待机配置→接收/采集复位分级释放→开流，随后才进入Linux，避免内置VDMA驱动在采集AXI仍复位时访问寄存器。整个启动仅加载一次PL。

## 验收

ISP穷举/节拍仿真；整体时序DRC；软件控制与缓冲归属测试；RAM启动确认SC130、DP链路、USB3，暗部曲线对比；同时运行速率/错误计数；USB开关对DP独立性；最终QSPI逐段散列读回、重启自主运行。区分硬件帧率、USB缓冲返回、实际显示新帧，记录偶发错误，不用平均fps掩盖卡顿。用户睡眠期间无法索取肉眼确认，使用连接状态、帧事件和本机预览检查，最终如实说明验证边界。

