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
