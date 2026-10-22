# Continuous SC130 DP + USB broker

Build in Linux/WSL: `make CC=aarch64-linux-gnu-gcc LDFLAGS=-static all`.
Run native regressions: `make OUT=/tmp/sc130-tests CC=gcc test`.

`build/sc130_platform_bridge` starts capture independently of a USB host. Optional arguments include `--capture /dev/videoN`, `--output /dev/videoN`, `--soft-connect PATH`, `--disable-dp`, and `--snapshot PATH`. SIGUSR1 saves one completed YUY2 frame to `/tmp/sc130-frame.yuy2` by default.

## Optional USB frame-order diagnostic

`--usb-sequence-marker` is **off by default**. It stamps only the UVC output copy, after the completed capture has been copied and before output QBUF. Capture memory, DP scanout and snapshots are unchanged.

In each raw 1280x1024 YUY2 frame, the first eight luma bytes at offsets `0,2,4,6,8,10,12,14` are:

- `53 43 31 30` hexadecimal (ASCII `SC10`).
- The capture source sequence as four little-endian bytes.

The eight interleaved chroma bytes at offsets `1,3,...,15` are set to128. Bytes from offset16 onward are untouched. This changes a tiny visible strip in diagnostic USB video. It is not enabled for production automatically.

For raw host capture, split at2621440 bytes per frame. Check the magic and reconstruct sequence with `Y[4] | Y[5]<<8 | Y[6]<<16 | Y[7]<<24`. Test increasing order using signed32-bit differences to allow wraparound; a positive gap indicates skipped source frames, zero indicates a duplicate, and a negative difference indicates reordered frames. Decode the raw YUY2 bytes before scaling or display processing.


## USB 性能诊断

`sc130_platform_bridge --profile` 在真实摄像头路径上启用分段耗时统计，每秒输出本次USB流累计数据。`cpu_one_core_pct=100` 表示一个CPU核心的时间；它不包含其他内核工作线程的CPU耗时。p50/p95/p99为固定桶的上界，max为实际采样最大值。

- copy_wall / copy_cpu：整帧memcpy墙钟与进程CPU耗时，含少量计时调用开销。
- capture_complete_to_dequeue：驱动软件完成时间戳到用户态取帧，不能称为SOF或曝光延迟。
- dequeue_to_submit：取出当前帧到输出QBUF返回，包含等待发送节奏、复制及QBUF。
- output_qbuf：输出QBUF调用耗时。
- usb_queue_to_return：调用输出QBUF到用户态DQBUF归还，包含内核排队、传输和用户态取完成的调度延迟，不是纯USB线缆时间。
- submit_gap：连续成功提交的时间间隔。

`--profile-static-usb` **仅用于对照实验**：每个USB输出槽只复制一次，随后发送冻结画面；真实采集和DP继续。不要用此模式验证真实摄像头新帧率，不要配置成自动启动。正常模式默认仍为真实图像CPU复制，缓冲所有权与缓存同步保持不变。

对照时保持同一应用/内核/DP/主机参数，主机使用512MiB采集缓冲，顺序运行真实复制A、冻结对照B、真实复制A2。主机完成退出后才停板端应用，避免人为制造停流错误。统计启动、停止边界与稳态错误须分别解释。结果与条件见 reports/usb_copy_performance_20261022.md。
