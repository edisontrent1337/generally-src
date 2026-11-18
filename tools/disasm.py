#!/usr/bin/env python3
"""Disassemble functions of the original exe, to pin down calling conventions for the rewrite.

usage: build/venv/bin/python tools/disasm.py ADDR [ADDR...]      (hex, e.g. 405f10)

Prints each function up to the next function start (analysis/functions.tsv), then a summary:
`ret N` (bytes popped -> Hook.pop), registers/XMM read before written (register arguments) and
stack argument offsets read relative to esp/ebp.
"""
import sys, os, re, bisect
import pefile, capstone

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
EXE = os.path.join(ROOT, "game/orig/GeneRally.exe")


def starts():
    s = []
    for line in open(os.path.join(ROOT, "analysis/functions.tsv")):
        if line.startswith("addr"):
            continue
        s.append(int(line.split("\t")[0], 16))
    return sorted(s)


def main():
    pe = pefile.PE(EXE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    st = starts()
    for arg in sys.argv[1:]:
        a = int(arg, 16)
        i = bisect.bisect_right(st, a)
        end = st[i] if i < len(st) else a + 0x400
        code = img[a - base:end - base]
        written, reg_in, rets, stack_in = set(), [], set(), set()
        print("== %08x .. %08x" % (a, end))
        for ins in md.disasm(code, a):
            print("  %08x  %-22s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
            rd, wr = ins.regs_access()
            for r in rd:
                n = ins.reg_name(r)
                base_n = {"cl": "ecx", "cx": "ecx", "dl": "edx", "dx": "edx", "al": "eax", "ax": "eax"}.get(n, n)
                if base_n in ("esp", "ebp", "eip", "eflags"):
                    continue
                if base_n not in written and base_n not in reg_in:
                    reg_in.append(base_n)
            for r in wr:
                written.add(ins.reg_name(r))
            if ins.mnemonic == "ret":
                rets.add(int(ins.op_str, 16) if ins.op_str else 0)
            for m in re.finditer(r"\[(esp|ebp) \+ (0x[0-9a-f]+|\d+)\]", ins.op_str):
                stack_in.add("%s+%s" % (m.group(1), m.group(2)))
        print("  ret pops: %s   regs read before write: %s" % (sorted(rets), " ".join(reg_in)))
        print("  stack refs: %s" % " ".join(sorted(stack_in)))


if __name__ == "__main__":
    main()
