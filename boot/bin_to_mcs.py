#!/usr/bin/env python3
"""Create and independently decode Intel HEX for a bounded single x4 QSPI image."""
import argparse
from pathlib import Path

def record(addr, kind, data):
    b = bytes([len(data), addr >> 8, addr & 255, kind]) + data
    return ':' + (b + bytes([-sum(b) & 255])).hex().upper()

def encode(data):
    if not data or len(data) > 32 * 1024 * 1024:
        raise ValueError('image must fit the 32 MiB flash')
    lines = []
    for offset in range(0, len(data), 16):
        if offset % 65536 == 0:
            lines.append(record(0, 4, (offset >> 16).to_bytes(2, 'big')))
        lines.append(record(offset & 65535, 0, data[offset:offset + 16]))
    lines.append(record(0, 1, b''))
    return '\n'.join(lines) + '\n'

def decode(text):
    image = bytearray()
    upper = 0
    ended = False
    for line in text.splitlines():
        if ended or not line.startswith(':'):
            raise ValueError('record after EOF or invalid prefix')
        b = bytes.fromhex(line[1:])
        if len(b) < 5 or len(b) != b[0] + 5 or sum(b) & 255:
            raise ValueError('record length/checksum')
        addr, kind, payload = int.from_bytes(b[1:3], 'big'), b[3], b[4:-1]
        if kind == 4 and len(payload) == 2 and addr == 0:
            upper = int.from_bytes(payload, 'big') << 16
        elif kind == 0:
            absolute = upper + addr
            if absolute != len(image) or absolute + len(payload) > 32*1024*1024:
                raise ValueError('noncontiguous, overlapping or out of range')
            image.extend(payload)
        elif kind == 1 and not payload and addr == 0:
            ended = True
        else:
            raise ValueError('unsupported record')
    if not ended:
        raise ValueError('missing EOF')
    return bytes(image)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary', type=Path)
    p.add_argument('mcs', type=Path)
    a = p.parse_args()
    data = a.binary.read_bytes()
    a.mcs.write_text(encode(data), encoding='ascii')
    if decode(a.mcs.read_text()) != data:
        raise SystemExit('MCS roundtrip failed')
    print(f'MCS_PASS bytes={len(data)} flash_limit=33554432')
