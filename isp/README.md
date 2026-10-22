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
