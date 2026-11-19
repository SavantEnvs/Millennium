#!/usr/bin/env python3
# Seeds for fuzz_star_decompress: one star_decompress block (u32 LE output size, then LZ4-style
# sequences: token = literal-length<<4 | (match-length - 4), 255-byte length extensions,
# u16 LE back-reference offset). Each seed drives one branch of star_parser.h:star_decompress,
# valid streams and each of its error exits, so the fuzzer starts from the whole decoder.
# No seed declares an output size of 0 (that input is a known UBSan defect, not a seed).
# Usage: gen_decompress_seeds.py mayhem/fuzz_star_decompress/testsuite
import os, struct, sys

def hdr(n): return struct.pack('<I', n)

def ext(n):
    # length-extension bytes for a value n >= 15 already counted in the token nibble
    out = bytearray()
    rest = n - 15
    while rest >= 255: out.append(255); rest -= 255
    out.append(rest)
    return bytes(out)

SEEDS = {
    # valid streams
    'lit_only': hdr(3) + b'\x30abc',
    'rle_offset1': hdr(9) + b'\x10a' + struct.pack('<H', 1) + b'\x40wxyz',
    'multi_seq': hdr(14) + b'\x22ab' + struct.pack('<H', 2) + b'\x20cd' + struct.pack('<H', 4) + b'\x10e',
    'long_lit_ext': hdr(300) + b'\xf0' + ext(300) + bytes(range(256)) + b'L' * 44,
    'long_match_ext': hdr(301) + b'\x1fa' + struct.pack('<H', 1) + ext(300 - 4),
    'lit_match_ext': hdr(40 + 600) + b'\xff' + ext(40) + b'0123456789' * 4 + struct.pack('<H', 40) + ext(600 - 4),
    'overlap_copy': hdr(4 + 20) + b'\x4fwxyz' + struct.pack('<H', 3) + bytes([20 - 4 - 15]),
    'header_only': hdr(16),
    # each error exit
    'too_short': b'\x01\x00\x00',
    'too_large': hdr(64 * 1024 * 1024 + 1) + b'\x10a',
    'lit_ext_overflow': hdr(20) + b'\xf0\xff\xff',
    'lit_overflow_out': hdr(1) + b'\x20ab',
    'lit_overflow_in': hdr(8) + b'\x50ab',
    'lit_ext_truncated': hdr(400) + b'\xf0\xff',
    'trunc_offset': hdr(8) + b'\x10a\x01',
    'zero_offset': hdr(8) + b'\x10a\x00\x00',
    'match_ext_truncated': hdr(400) + b'\x1fa\x01\x00\xff',
    'match_ext_overflow': hdr(30) + b'\x1fa\x01\x00\xff',
    'match_overflow': hdr(3) + b'\x10a\x01\x00',
    'bad_offset': hdr(16) + b'\x10a\x05\x00',
}

def main(out):
    os.makedirs(out, exist_ok=True)
    for name, data in SEEDS.items():
        with open(os.path.join(out, name), 'wb') as f: f.write(data)

if __name__ == '__main__':
    main(sys.argv[1])
