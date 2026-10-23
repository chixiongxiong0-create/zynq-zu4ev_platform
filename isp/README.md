# SC130 black correction + tone ISP

The active capture path is `raw10_to_yuy2` (interface wrapper) → `sc130_tonemap_yuy2` (adapted attachment core). Both source files under `isp/` must be compiled. Files under `vendor/` remain unchanged reference originals and must not be added as duplicate modules.

Default black level is **52 RAW10 codes**, from the 2026-10-21 fully covered sensor measurement. Three post-tone frames measured mean Y≈55.2 and median Y=55; inverse lookup under the previous curve gives a RAW median interval52..53. This is a calibration for the tested exposure/gain/temperature, not a universal sensor constant.

The new curve computes:

```
corrected = max(raw10 - 52, 0)
index = round_half_up(corrected * 1023 / 971)
Y = original_vendor_lut[index]
```

All operations are folded into a 1024-entry ROM at generation time. Hardware performs two parallel lookups per accepted clock; it has no runtime divider or added pipeline stage. Inputs0..52 map to legal video black16, input1023 maps to235, U/V remain128. The black correction precedes the nonlinear curve mathematically; subtracting52 from already mapped Y would be incorrect.

`sc130_tonemap_yuy2` retains the attachment's two-pixel packing and one-cycle registered output, adds a clock enable so the capture wrapper can hold an unconsumed transaction, and embeds its ROM initialization to avoid `.mem` working-directory dependencies. The production camera path keeps ready high. Marker and fault alignment stays at one clock. `BYPASS=1` in the wrapper preserves the uncorrected legacy linear path for A/B diagnostics; normal synthesis uses0.

Reproduce from any directory:

```
python /absolute/project/path/isp/generate_rtl.py --black 52
```

Outputs: `sc130_tonemap_yuy2.v`, `black_corrected_tone_lut.mem`, `tone_config.json`. The wrapper is hand-maintained. The generator uses only the Python standard library and preserves the supplied quantized vendor LUT. Other black levels require regeneration and a new bitstream.

Verification: `sim/isp/run.ps1`, XSim2025.1 explicit source list. The pre-change RTL fails the new black correction test at66ns (`reports/isp/black_red_run.log`). The corrected implementation passes3424 checked cycles plus8 stalled cycles, all1024 codes in both lane orderings, black/white anchors, monotonic vendor LUT, legacy bypass, invalid gaps, repeated reset, reset while stalled, SOF/EOL, sustained1280-pair traffic and the existing one-stage packet/global-fault alignment model. This is RTL evidence; final physical and board checks are reported separately.

Sensor automatic BLC/RNC stays enabled. Their nonzero output target is deliberately accounted for in the FPGA curve rather than changed blindly. Auto exposure is enabled in the FSBL initialization table; automatic gain stays disabled. ROM correction does not perform automatic exposure, local contrast enhancement, denoising or lens flare removal.


## ISP统一顶层

采集模块统一例化 `isp.v` 中的 `isp`。当前层次为：

```text
isp
└─ raw10_2_yuv_i : raw10_to_yuy2
   ├─ BYPASS=0：sc130_tonemap_yuy2（黑位52校正、tone mapping、YUY2打包）
   └─ BYPASS=1：旧线性RAW10→YUY2
```

RAW输入每拍两个10bit像素，输出内存字节顺序Y0/80/Y1/80。新顶层只组织模块，不增加寄存器，因此保持一级流水、现有READY/VALID和SOF/EOL行为。当前tone mapping和打包仍由已有模块实现，不重复做一次亮度映射。

后续RAW域算法插在转换器之前，YUV域算法插在之后；每一级都应保持像素与SOF/EOL对齐，并传递背压。当前摄像头入口持续广播、不等待READY，应由采集FIFO吸收下游短暂停顿。若新增流水级，必须同步调整uvc_capture_dual.v与uvc_capture_path.v中的packet_error/global_fault延迟，不能只改ISP像素延迟。

sim/isp/tb_isp.sv同时运行新isp入口与旧raw10_to_yuy2入口，每拍对比正常和BYPASS路径，覆盖全部1024灰度、两像素通道、复位、空拍、SOF/EOL、连续输出和8拍背压保持。构建脚本已包含isp.v；生成LUT不会覆盖此顶层。vendor目录不变。
