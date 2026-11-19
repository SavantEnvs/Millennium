#!/usr/bin/env python3
"""Writes valid .star plugin packages (format version 2) for seeds and the self-test.

Layout mirrors src/engine/star_parser.cc: optional u32-LE shim prefix, 12-byte header
('STAR', version, ?, section count, flags, FNV-1a hash of the shim), 256-byte section
entries, sections encoded as compress -> parity weave -> XOR (decoder runs the reverse).
Usage: gen_star_seeds.py <out dir>
"""
import os, struct, sys

def fnv_step(h, b): return ((h ^ b) * 0x01000193) & 0xFFFFFFFF
def fnv_bytes(data):
    h = 0x811c9dc5
    for b in data: h = fnv_step(h, b)
    return h
def fnv_u32(h, v):
    for i in range(4): h = fnv_step(h, (v >> (8 * i)) & 0xFF)
    return h

def weave(data):
    out, roll, idx = bytearray(), 0xA7C3E91F, 0
    for pos in range(0, len(data), 64):
        chunk = data[pos:pos + 64]
        x = 0
        for b in chunk: x ^= b
        roll = fnv_u32(roll, x)
        out += chunk + struct.pack('<III', x, roll, idx)
        idx += 1
    return bytes(out)

def xor(data): return bytes(b ^ 0x4D ^ (i & 0xFF) for i, b in enumerate(data))

def compress(data, match=True):
    # literal run, then (match=True) one 8-byte match copying from offset 4
    out = bytearray(struct.pack('<I', len(data) + (8 if match else 0)))
    n = len(data)
    if n >= 15:
        out.append(0xF4)
        rest = n - 15
        while rest >= 255: out.append(255); rest -= 255
        out.append(rest)
    else:
        out.append((n << 4) | 4)
    out += data
    if match: out += struct.pack('<H', 4)
    return bytes(out)

def sub_entries(entries):
    out = bytearray(struct.pack('<I', len(entries)))
    for name, data in entries:
        out += struct.pack('<HI', len(name), len(data)) + name + data
    return bytes(out)

def msgpack_map(d):
    out = bytearray([0x80 | len(d)])
    for k, v in d.items():
        for s in (k, v):
            s = s.encode(); out += bytes([0xA0 | len(s)]) + s
    return bytes(out)

def encode(data, flags, match=True):
    if flags == 0: return data
    if flags & 0x80: data = compress(data, match)
    data = weave(data)
    if flags & 0x40: data = xor(data)
    return data

def star(sections, shim=b'', star_flags=0):
    hdr_len = 12 + 256 * len(sections)
    body, table, off = bytearray(), bytearray(), hdr_len
    for sid, flags, meta_flags, raw in sections:
        enc = encode(raw, flags, match=(sid != 1))  # metadata is strict msgpack: no trailing bytes
        e = bytearray(256)
        e[0], e[1] = sid, flags
        struct.pack_into('<IQQI', e, 4, meta_flags, off, len(enc), 0)
        table += e; body += enc; off += len(enc)
    hdr = b'STAR' + bytes([2, 0, len(sections), star_flags]) + struct.pack('<I', fnv_bytes(shim) if shim else 0)
    s = hdr + table + body
    return (struct.pack('<I', len(shim)) + shim + s) if shim else s

META = msgpack_map({'id': 'demo-plugin', 'name': 'Demo Plugin', 'version': '1.2.3', 'entry': 'main.lua'})
JS = b'console.log("millennium frontend");'
WK = b'document.title = "webkit";'
ASSETS = sub_entries([(b'img/a.png', compress(b'PNGDATA!')), (b'dir/', b'')])

def main(out):
    os.makedirs(out, exist_ok=True)
    seeds = {
        'plugin.star': star([(1, 0, 0, META), (2, 0, 0, b'return {}'),
                             (3, 0xC0, 0, sub_entries([(b'bundle.js', JS)])),
                             (4, 0x40, 0, sub_entries([(b'other/', b''), (b'wk.js', WK)])),
                             (5, 0, 1, ASSETS)]),
        'shim.star': star([(1, 0x80, 0, META), (3, 0, 0, sub_entries([(b'bundle.js', JS)]))], shim=b'#!/bin/sh\nexit 0\n'),
        'signed.star': star([(1, 0xC0, 0, META)], star_flags=1) + bytes(64),
    }
    for name, data in seeds.items():
        with open(os.path.join(out, name), 'wb') as f: f.write(data)

if __name__ == '__main__':
    main(sys.argv[1])
