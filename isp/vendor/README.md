# SC130 monochrome RAW10 → YUY2: FPGA tone-mapping starter

## Files

- `gen_tone_lut.py`: generate `sc130_tone_lut.mem` using the piecewise tone-mapping curve.
- `sc130_tone_lut.mem`: pre-generated 1024x8 lookup table, BLC offset 0 and white anchor 1023.
- `sc130_tonemap_yuy2.v`: synchronous dual-read ROM plus YUY2 packer (2 pixels per clock).

## Curve (limited-range Y)

| Normalized input | Output Y |
|---:|---:|
|0|16|
|0.01|22|
|0.04|49|
|0.10|82|
|0.25|128|
|0.50|176|
|0.75|208|
|0.90|224|
|1.0|235|

Linear interpolation connects the knots. Monotonic; output range [16,235].

## Generate ROM

From this directory:

```bash
python gen_tone_lut.py --black 0 --white 1023
```

If dark-frame measurement shows a sensor black code of 64 (illustrative only),
regenerate using `--black 64 --white 959`. Do not hardcode 64 without measuring.
`white` is the anchor after black-level subtraction, not the original raw code.

For *explicitly negotiated* full-range YUY2:

```bash
python gen_tone_lut.py --black 0 --range full
```

## FPGA hookup

- Two 10-bit RAW pixels each cycle, `s_raw0` even X and `s_raw1` odd X.
- One-cycle output pipeline, no stream backpressure support in starter design.
- For 1280x1024 active area, 640 valid input pairs per line and 1024 lines.
- Output bytes in little-endian order: `Y0, 0x80, Y1, 0x80`.
- Assert EOL with 640th pair and SOF with first pair of frame.
- Timing, CDC, RAW10 unpacking, and AXI-stream flow control handled upstream/downstream.
- Static ROM is deliberately not writable. Use bank-switched dual-port BRAMs for
  frame-to-frame auto tone mapping; do not update the active ROM midframe.

## Dynamic white anchor (optional)

Accumulate histogram samples from BLC-corrected RAW, preferably subsampled
1/16, and find percentile P99.95. For the first trial set
`F = 1023-black_level`, `W_target = min(F, max(P99.95+24, ceil(0.85*F)))`.
Move W rapidly upward if highlights increase (coefficient 1/2) and slowly
lower it when highlight brightness decreases (coefficient 1/16). Keep last good
value if histogram is invalid. Recalculate 1024-entry LUT into INACTIVE bank
and swap banks on SOF only.

Always inspect raw saturation independently; clipped sensor data cannot
be reconstructed by tone mapping.

## Note on CLAHE

CLAHE can help reveal detail in very dark local regions, but requires many
histograms and multi-region interpolation. Begin with global ROM first.
The provided implementation is **global** tone mapping only.
