#!/usr/bin/env python3
"""Print constants from the original exe's image: rodata.py ADDR[:f|d|i|s] ...  (default f = float)"""
import struct, sys, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
d = open(os.path.join(ROOT, "game/orig/GeneRally.exe"), "rb").read()
pe = struct.unpack_from("<I", d, 0x3c)[0]
n = struct.unpack_from("<H", d, pe + 6)[0]
opt = struct.unpack_from("<H", d, pe + 20)[0]
secs = [struct.unpack_from("<IIII", d, pe + 24 + opt + 40 * i + 8) for i in range(n)]
def raw(a):
    a -= 0x400000
    for vs, va, rs, ra in secs:
        if va <= a < va + rs: return ra + a - va
    return None
for arg in sys.argv[1:]:
    a, _, k = arg.partition(":")
    o = raw(int(a, 16)); k = k or "f"
    if o is None: print(a, "not in file (bss)"); continue
    if k == "s": print(a, repr(d[o:d.index(b"\0", o)].decode("latin-1")))
    else: print(a, struct.unpack_from("<" + k, d, o)[0])
