#!/usr/bin/env python3
"""Where is an original function/global referenced?  tools/xrefs.py ADDR...

Scans the exe for direct calls/jumps (E8/E9 rel32) and for the address as an absolute 32-bit value
(vtables, function pointer tables, push imm32, mov reg/mem imm32), and names the containing function.
"""
import bisect, os, struct, sys
import pefile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
pe = pefile.PE(os.path.join(ROOT, "game/orig/GeneRally.exe"), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
starts = sorted(int(l.split("\t")[0], 16) for l in open(os.path.join(ROOT, "build/funcinfo.tsv")))


def owner(va):
    i = bisect.bisect_right(starts, va) - 1
    return "%08x" % starts[i] if i >= 0 else "?"


def section(va):
    for s in pe.sections:
        if s.VirtualAddress <= va - base < s.VirtualAddress + s.Misc_VirtualSize:
            return s.Name.rstrip(b"\0").decode()
    return "?"


for arg in sys.argv[1:]:
    t = int(arg, 16)
    out = []
    pat = struct.pack("<I", t)
    i = img.find(pat)
    while i >= 0:
        va = base + i
        sec = section(va)
        out.append("  abs  %08x %-6s %s" % (va, sec, "in " + owner(va) if sec == ".text" else ""))
        i = img.find(pat, i + 1)
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    lo = text.VirtualAddress
    for i in range(lo, lo + text.Misc_VirtualSize - 5):
        if img[i] in (0xE8, 0xE9):
            rel = struct.unpack_from("<i", img, i + 1)[0]
            if base + i + 5 + rel == t:
                out.append("  %s %08x in %s" % ("call" if img[i] == 0xE8 else "jmp ", base + i, owner(base + i)))
    print("%08x:" % t)
    print("\n".join(out) if out else "  no references")
