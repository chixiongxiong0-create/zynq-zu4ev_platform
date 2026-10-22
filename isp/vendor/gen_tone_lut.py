#!/usr/bin/env python3
"""Generate a 1024x8-bit direct RAW10 -> YUY2-luma tone-mapping ROM.

Y values default to studio/limited range [16, 235]. Input offset is the
measured camera black level, not an assumed SC130GS-specific constant.

Examples:
  python gen_tone_lut.py
  python gen_tone_lut.py --black 64 --white 959
  python gen_tone_lut.py --range full --output full.mem
"""
import argparse
from pathlib import Path
import numpy as np

# Normalized post-black-correction RAW value -> video-legal luma
X = np.array([0, 0.01, 0.04, 0.10, 0.25, 0.50, 0.75, 0.90, 1.0], dtype=float)
Y_STUDIO = np.array([16, 22, 49, 82, 128, 176, 208, 224, 235], dtype=float)


def create_lut(black: int = 0, white: int | None = None, video_range="limited"):
    if not 0 <= black <= 1022:
        raise ValueError("black must be between 0 and 1022")
    if white is None:
        white = 1023 - black
    if not 1 <= white <= 1023:
        raise ValueError("white must be between 1 and 1023")
    code = np.arange(1024, dtype=np.float64)
    corrected = np.maximum(code - black, 0.0)
    normalized = np.clip(corrected / white, 0.0, 1.0)
    reference = np.interp(normalized, X, Y_STUDIO)
    if video_range == "limited":
        y = reference
    else:
        y = (reference - 16.0) * (255.0 / 219.0)
    lut = np.rint(y).clip(0, 255).astype(np.uint8)
    if np.any(np.diff(lut.astype(np.int16)) < 0):
        raise RuntimeError("LUT not monotonic")
    return lut


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--black", type=int, default=0,
                        help="Measured RAW10 black-level offset, default 0")
    parser.add_argument("--white", type=int, default=None,
                        help="White anchor after black correction; default 1023-black")
    parser.add_argument("--range", choices=("limited", "full"), default="limited")
    parser.add_argument("--output", type=Path, default=Path("sc130_tone_lut.mem"))
    args = parser.parse_args()
    lut = create_lut(args.black, args.white, args.range)
    args.output.write_text("".join(f"{int(v):02X}\n" for v in lut), encoding="ascii")
    print(f"Wrote {len(lut)} entries to {args.output}; range {lut.min()}..{lut.max()}")
    for code in (0, 64, 128, 256, 512, 768, 896, 1023):
        print(f"  raw10={code:4d} -> Y={int(lut[code]):3d}")


if __name__ == "__main__":
    main()
