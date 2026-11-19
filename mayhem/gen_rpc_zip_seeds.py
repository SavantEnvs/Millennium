#!/usr/bin/env python3
"""Writes the seed corpora for fuzz_lua_rpc (parent->lua-host reply frames) and
fuzz_zip_extract (plugin/theme archives). Run from the repo root: python3 mayhem/gen_rpc_zip_seeds.py"""
import io, os, struct, zipfile


def mp(o):  # minimal msgpack encoder (the subset nlohmann::json::from_msgpack reads back)
    if o is None: return b"\xc0"
    if o is True: return b"\xc3"
    if o is False: return b"\xc2"
    if isinstance(o, int):
        if 0 <= o < 128: return bytes([o])
        if -32 <= o < 0: return struct.pack("b", o)
        return b"\xd3" + struct.pack(">q", o)
    if isinstance(o, float): return b"\xcb" + struct.pack(">d", o)
    if isinstance(o, str):
        b = o.encode()
        return (bytes([0xa0 | len(b)]) if len(b) < 32 else b"\xd9" + bytes([len(b)])) + b
    if isinstance(o, list):
        return bytes([0x90 | len(o)]) + b"".join(mp(x) for x in o)
    if isinstance(o, dict):
        return bytes([0x80 | len(o)]) + b"".join(mp(k) + mp(v) for k, v in o.items())
    raise TypeError(o)


def frame(msg):
    p = mp(msg)
    return struct.pack("<I", len(p)) + p


def res(i, result):
    return frame({"type": "res", "id": i, "result": result})


def write(d, name, data):
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data)


R = "mayhem/fuzz_lua_rpc/testsuite"
obj = {"a": [1, 2.5, "x", None, True], "b": {"c": {"d": [[], {}]}}, "n": -7}
write(R, "object.bin", res(1, {"success": True, "returnJson": {"type": "object", "value": obj}})
      + res(2, {"success": True, "value": "v"}) + res(3, {"success": True, "values": {"k": 1}}))
write(R, "string.bin", res(1, {"success": True, "returnJson": {"type": "string", "value": "hello"}}))
write(R, "error.bin", res(1, {"success": False, "error": "boom"}))
write(R, "notify_then_res.bin", frame({"type": "notify", "method": "m", "params": [1]})
      + res(2, {}) + res(1, {"success": True, "returnJson": {"type": "number", "value": 3.5}}))
write(R, "deep.bin", res(1, {"success": True, "returnJson": {"type": "object", "value": [[[[[["deep"]]]]]]}}))

Z = "mayhem/fuzz_zip_extract/testsuite"
for name, comp in (("deflate.zip", zipfile.ZIP_DEFLATED), ("stored.zip", zipfile.ZIP_STORED)):
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w", comp) as z:
        z.writestr(zipfile.ZipInfo("plugin/", (2024, 1, 1, 0, 0, 0)), b"")
        z.writestr(zipfile.ZipInfo("plugin/plugin.json", (2024, 1, 1, 0, 0, 0)), b'{"name":"p","version":"1.0.0"}', comp)
        z.writestr(zipfile.ZipInfo("plugin/backend/main.lua", (2024, 1, 1, 0, 0, 0)), b"return {}\n" * 20, comp)
        z.writestr(zipfile.ZipInfo("theme/skin.css", (2024, 1, 1, 0, 0, 0)), b":root{--c:#fff}\n", comp)
    write(Z, name, b.getvalue())
