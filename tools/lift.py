#!/usr/bin/env python3
"""Static recompiler: Ghidra p-code export (tools/ghidra/ExportPcode.java) -> portable C.

Every x86 function becomes `void ORIG_xxxxxx(CPU *c)`, reached through a weak `F_xxxxxx` (weak.c) that
rewritten code can override. Guest registers live in c->r (byte array
laid out like Ghidra's x86 register space), guest memory is the 4 GB arena MEM (guest address
== offset). Calls push the real return address on the guest stack, so stack layout matches the
original exactly; RETURN stores the popped address in c->ret and the caller checks it.

usage: lift.py <export.pcode> <outdir> [exe for relocation-target entries]
"""
import sys, os, re, collections

RAMSPACE_ID = None  # first input of LOAD/STORE; ignored

class Insn:
    __slots__ = ("addr", "len", "text", "flows", "ops")
    def __init__(s, addr, ln, text):
        s.addr, s.len, s.text, s.flows, s.ops = addr, ln, text, [], []

def vn(tok):
    if tok == "-":
        return None
    sp, off, sz = tok.split(":")
    return (sp, int(off, 16), int(sz))

def load(path):
    insns, fns, regs, blocks = {}, {}, {}, []
    cur = None
    with open(path) as f:
        for line in f:
            t = line[0]
            if t == "P":
                parts = line.split()
                other = None
                if parts[-1].startswith("#"):
                    other = parts.pop()[1:]
                cur.ops.append((parts[1], vn(parts[2]), [vn(x) for x in parts[3:]], other))
            elif t == "I":
                head, _, text = line.rstrip("\n").partition(" | ")
                _, a, ln, _bytes = head.split()
                cur = Insn(int(a, 16), int(ln), text)
                insns[cur.addr] = cur
            elif t == "F" and line[1] == " ":
                for x in line.split()[1:]:
                    if not x.startswith("EXTERNAL"):
                        cur.flows.append(int(x, 16))
            elif line.startswith("FN "):
                _, a, name = line.split(None, 2)
                fns[int(a, 16)] = name.strip()
            elif t == "R":
                _, name, off, sz = line.split()
                regs.setdefault((int(off, 16), int(sz)), name)
            elif t == "B":
                _, name, s, e, x = line.split()
                blocks.append((name, int(s, 16), int(e, 16), x == "x"))
    return insns, fns, regs, blocks

# ---------------------------------------------------------------- types / expressions
T = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t", 8: "uint64_t", 10: "uint64_t", 16: "u128"}
S = {1: "int8_t", 2: "int16_t", 4: "int32_t", 8: "int64_t", 10: "int64_t", 16: "i128"}
BITS = {1: 8, 2: 16, 4: 32, 8: 64, 10: 64, 16: 128}

def lit(v, sz):
    if sz == 16:
        return "((u128)0x%xull)" % v if v < (1 << 64) else "(((u128)0x%xull<<64)|0x%xull)" % (v >> 64, v & (2**64 - 1))
    if sz in (8, 10):
        return "0x%xull" % v
    if sz == 4:
        return "0x%xu" % v
    return "((%s)0x%x)" % (T[sz], v)

def regx(off, sz):
    if sz == 10:
        sz = 8  # x87 values are host doubles stored in the first 8 bytes
    return "R%d(0x%x)" % (sz, off)

GPRS = {0x0: "eax", 0x4: "ecx", 0x8: "edx", 0xc: "ebx", 0x10: "esp", 0x14: "ebp", 0x18: "esi", 0x1c: "edi"}
CFLAGS = (0x200, 0x202, 0x204, 0x206, 0x207, 0x20b)

def reg_units(off, sz):
    """Cacheable units covered by register varnode (off,sz): list of units, [] if it touches none,
    None if it overlaps cacheable state in a way the cache cannot represent."""
    if sz == 10:
        sz = 8
    end = off + sz
    if end <= 0x20 and off >= 0:
        base = off & ~3
        if end > base + 4:
            return None
        return [("g", base)]
    if off < 0x20 < end:
        return None
    if 0x200 <= off < 0x20c or (off < 0x200 < end):
        if sz == 1 and off in CFLAGS:
            return [("f", off)]
        if sz == 1:
            return []  # other flag bytes (TF/IF/DF...) stay in memory
        return None
    if 0x1200 <= off < 0x1400:  # XMMn at 0x1200 + 0x40 n (Ghidra leaves room for YMM/ZMM)
        n, lo = (off - 0x1200) // 0x40, (off - 0x1200) % 0x40
        if lo % 4 or sz % 4 or lo + sz > 16:
            return None
        return [("x", n, lo // 4 + k) for k in range(sz // 4)]
    if off < 0x1200 < end or off < 0x1400 < end:
        return None
    return []

def unit_name(u):
    if u[0] == "g": return "r_" + GPRS[u[1]]
    if u[0] == "f": return "f_%x" % u[1]
    return "x%d_%d" % (u[1], u[2])

def unit_mem(u):
    if u[0] == "g": return "R4(0x%x)" % u[1]
    if u[0] == "f": return "R1(0x%x)" % u[1]
    return "R4(0x%x)" % (0x1200 + 0x40 * u[1] + 4 * u[2])

class Fn:
    def __init__(self, entry):
        self.entry = entry
        self.uniq = {}
        self.cache = False
        self.units = set()
        self.ubuf = {}
        self.usize = 0
        self.dirty = None   # units modified since the last sync (None = unknown -> all)

    def units_written(self, v):
        if not self.cache or v is None or v[0] != "r":
            return ()
        return reg_units(v[1], v[2]) or ()

    def plan_cache(self, body, insns):
        units = set()
        if os.environ.get("LIFT_NOCACHE"):
            self.cache = False
            return
        for a in body:
            for (opc, out, inp, oth) in insns[a].ops:
                for v in [out] + inp:
                    if v and v[0] == "r":
                        u = reg_units(v[1], v[2])
                        if u is None:
                            self.cache = False
                            self.units = set()
                            return
                        units.update(u)
        self.cache = True
        self.units = units

    def sync_out(self):
        if not self.cache: return ""
        us = self.units if self.dirty is None else (self.dirty & self.units)
        if not us: return ""
        return " ".join("%s = %s;" % (unit_mem(u), unit_name(u)) for u in sorted(us)) + " "

    def sync_in(self):
        if not self.cache: return ""
        return " " + " ".join("%s = %s;" % (unit_name(u), unit_mem(u)) for u in sorted(self.units))

    def decls(self):
        if not self.cache: return []
        out = []
        for u in sorted(self.units):
            out.append("  %s %s = %s;" % ("uint8_t" if u[0] == "f" else "uint32_t", unit_name(u), unit_mem(u)))
        return out

    def creg_read(self, off, sz):
        us = reg_units(off, sz)
        if us is None or not us:
            return None
        u = us[0]
        if u[0] == "g":
            sh = 8 * (off - u[1])
            v = unit_name(u) if not sh else "(%s >> %d)" % (unit_name(u), sh)
            return v if sz == 4 else "(%s)%s" % (T[sz], v)
        if u[0] == "f":
            return unit_name(u)
        if len(us) == 1:
            return unit_name(u)
        if len(us) == 2:
            return "(((uint64_t)%s << 32) | %s)" % (unit_name(us[1]), unit_name(us[0]))
        return "(((u128)%s << 96) | ((u128)%s << 64) | ((u128)%s << 32) | (u128)%s)" % tuple(unit_name(x) for x in reversed(us))

    def creg_write(self, off, sz, e):
        us = reg_units(off, sz)
        if us is None or not us:
            return None
        u = us[0]
        if u[0] == "g":
            n = unit_name(u)
            if sz == 4:
                return "%s = %s;" % (n, e)
            sh = 8 * (off - u[1])
            mask = ((1 << (8 * sz)) - 1) << sh
            return "%s = (%s & 0x%xu) | ((uint32_t)(%s)(%s) << %d);" % (n, n, 0xffffffff & ~mask, T[sz], e, sh)
        if u[0] == "f":
            return "%s = %s;" % (unit_name(u), e)
        if len(us) == 1:
            return "%s = %s;" % (unit_name(u), e)
        tmp = "{ %s t_ = %s; " % (T[sz], e)
        for k, x in enumerate(us):
            tmp += "%s = (uint32_t)(t_ >> %d); " % (unit_name(x), 32 * k)
        return tmp + "}"

    def plan_uniques(self, ins):
        """Temporaries accessed with overlapping (offset,size) pairs go into the byte buffer U."""
        vs = set()
        for (opc, out, inp, oth) in ins.ops:
            for v in [out] + inp:
                if v and v[0] == "u":
                    vs.add((v[1], 8 if v[2] == 10 else v[2]))
        self.ubuf = {}
        vs = sorted(vs)
        groups, cur, end = [], [], -1
        for (o, z) in vs:
            if cur and o < end:
                cur.append((o, z)); end = max(end, o + z)
            else:
                if cur: groups.append(cur)
                cur, end = [(o, z)], o + z
        if cur: groups.append(cur)
        pos = 0
        for g in groups:
            if len(g) < 2:
                continue
            base = g[0][0]
            span = max(o + z for o, z in g) - base
            for (o, z) in g:
                self.ubuf[(o, z)] = pos + (o - base)
            pos += (span + 15) & ~15
        self.usize = max(self.usize, pos)

    def rd(self, v):
        sp, off, sz = v
        if sp == "c":
            return lit(off, sz)
        if sp == "u":
            k = self.ubuf.get((off, 8 if sz == 10 else sz))
            if k is not None:
                return "U%d(%d)" % (8 if sz == 10 else sz, k)
            n = "u%x_%d" % (off, sz)
            self.uniq[n] = T[sz]
            return n
        if sp == "r":
            if self.cache:
                e = self.creg_read(off, sz)
                if e is not None:
                    return e
            return regx(off, sz)
        if sp == "m":
            if sz == 10:
                return "ld_f80(m, 0x%xu)" % off
            return "M%d(0x%xu)" % (sz, off)
        raise ValueError("space %s" % sp)

    def wr(self, v, expr):
        sp, off, sz = v
        if sp == "r" and self.cache:
            st = self.creg_write(off, sz, expr)
            if st is not None:
                return st
        if sp == "m" and sz == 10:
            return "st_f80(m, 0x%xu, %s);" % (off, expr)
        return "%s = %s;" % (self.rd(v), expr)

def reloc_targets(exe):
    """Values of all HIGHLOW base-relocation fixups in a PE file."""
    import struct
    d = open(exe, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", d, opt + 28)[0]
    rel_rva, rel_sz = struct.unpack_from("<II", d, opt + 96 + 5 * 8)
    so = opt + struct.unpack_from("<H", d, pe + 20)[0]
    secs = [struct.unpack_from("<8sIIII", d, so + 40 * i) for i in range(nsec)]
    def r2o(rva):
        for (_, vs, va, rs, ro) in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro
        return None
    out = set()
    p, end = r2o(rel_rva), r2o(rel_rva) + rel_sz
    while p < end:
        page, size = struct.unpack_from("<II", d, p)
        if size < 8:
            break
        for k in range((size - 8) // 2):
            e = struct.unpack_from("<H", d, p + 8 + 2 * k)[0]
            if e >> 12 == 3:
                o = r2o(page + (e & 0xFFF))
                if o is not None:
                    out.add(struct.unpack_from("<I", d, o)[0])
        p += size
    return out

# ---------------------------------------------------------------- lifting
def main():
    src, outdir = sys.argv[1], sys.argv[2]
    insns, fns, regs, blocks = load(src)
    xblocks = [(s, e) for (_, s, e, x) in blocks if x]
    def in_code(a):
        return any(s <= a <= e for s, e in xblocks)

    # entries: Ghidra functions + every direct call target
    entries = set(a for a in fns if a in insns)
    for ins in insns.values():
        for (opc, out, inp, oth) in ins.ops:
            if opc == "CALL" and inp[0][0] == "m" and inp[0][1] in insns:
                entries.add(inp[0][1])
    # code addresses stored anywhere in the image (relocation targets): vtables, callbacks, initializer
    # tables. Switch-table targets stay inside their function.
    if len(sys.argv) > 3:
        jt = set()
        for ins in insns.values():
            if ins.ops and ins.ops[-1][0] == "BRANCHIND":
                jt.update(ins.flows)
        added = 0
    hard = set(entries)  # body boundaries: Ghidra functions + direct call targets
    if len(sys.argv) > 3:
        for t in reloc_targets(sys.argv[3]):
            if t in insns and t not in hard and t not in jt:
                entries.add(t)
                added += 1
        print("entries from relocations:", added, file=sys.stderr)
    entries = sorted(entries)
    entryset = set(entries)
    print("functions:", len(entries), "(boundaries %d)" % len(hard), file=sys.stderr)

    os.makedirs(outdir, exist_ok=True)
    stats = collections.Counter()

    def successors(ins):
        """(list of in-function successor addrs) for body discovery."""
        succ = []
        nxt = ins.addr + ins.len
        ops = ins.ops
        if is_special(ins):
            sp = special_kind(ins)
            return [] if sp == "trap" else [nxt]
        falls = True
        for i, (opc, out, inp, oth) in enumerate(ops):
            if opc in ("BRANCH", "CBRANCH"):
                t = inp[0]
                if t[0] == "m":
                    succ.append(t[1])
                elif t[0] == "c":
                    rel = t[1] if t[1] < 0x80000000 else t[1] - (1 << 32)
                    if i + rel >= len(ops):
                        succ.append(nxt)
        last = ops[-1][0] if ops else None
        if last in ("BRANCH", "RETURN"):
            falls = False
        if last == "BRANCHIND":
            falls = False
            succ.extend(ins.flows)
        if falls:
            succ.append(nxt)
        return succ

    def is_special(ins):
        return special_kind(ins) is not None

    def special_kind(ins):
        tx = ins.text
        if tx.startswith("CPUID"):
            return "cpuid"
        if tx.startswith("XGETBV"):
            return "xgetbv"
        for (opc, out, inp, oth) in ins.ops:
            if opc == "CALLOTHER" and oth == "swi":
                return "trap"
        return None

    hardset = hard
    def collect(entry):
        body, todo = set(), [entry]
        while todo:
            a = todo.pop()
            if a in body:
                continue
            if a != entry and a in hardset:
                continue  # tail call / jump into another function: handled at emit time
            ins = insns.get(a)
            if ins is None:
                continue
            body.add(a)
            for s in successors(ins):
                if s not in body:
                    todo.append(s)
        return sorted(body)

    funcs_out = []
    files = collections.defaultdict(list)
    for fi, entry in enumerate(entries):
        body = collect(entry)
        bodyset = set(body)
        f = Fn(entry)
        f_dead = dead_flag_writes(body, bodyset, insns, entry, hardset, successors)
        f.plan_cache(body, insns)
        stats["cached_fns"] += f.cache
        dirty_in = dirty_sets(f, body, bodyset, insns, entry, hardset, successors) if f.cache else {}
        stats["dead_flag_writes"] += len(f_dead)
        lines = []
        # labels needed
        need = {entry}
        order = {a: i for i, a in enumerate(body)}

        def target_code(t, tail_ok=True):
            """C statement to transfer control to absolute address t."""
            if t in bodyset and not (t != entry and t in hardset):
                need.add(t)
                return "goto L_%x;" % t
            if t in hardset:
                stats["tailcall"] += 1
                return "{ %sF_%x(c); return; }" % (f.sync_out(), t)
            stats["jump_out"] += 1
            return "{ %srt_jump(c, 0x%xu); return; }" % (f.sync_out(), t)

        for idx, a in enumerate(body):
            ins = insns[a]
            nxt = a + ins.len
            lines.append(("L", a))
            lines.append(("C", "/* %06x: %s */" % (a, ins.text.replace("*/", "* /"))))
            f.dirty = set(dirty_in.get(a, ())) if f.cache else None
            sk = special_kind(ins)
            if sk == "cpuid":
                lines.append(("C", f.sync_out() + "rt_cpuid(c);" + f.sync_in()))
                f.dirty = set() if f.cache else None
            elif sk == "xgetbv":
                lines.append(("C", f.wr(("r", 0, 4), "7u") + " " + f.wr(("r", 8, 4), "0u")))
            elif sk == "trap":
                lines.append(("C", f.sync_out() + "rt_trap(c, 0x%xu); return;" % a))
                continue
            else:
                emit_ops(f, ins, lines, target_code, hardset, insns, stats, bodyset, f_dead)
            # fallthrough
            ops = ins.ops
            last = ops[-1][0] if ops and sk is None else None
            if sk == "trap" or last in ("BRANCH", "RETURN", "BRANCHIND"):
                continue
            if idx + 1 < len(body) and body[idx + 1] == nxt:
                continue
            lines.append(("C", fall_code(nxt, bodyset, entry, hardset, insns, need, stats, f)))

        # render
        out = ["void ORIG_%x(CPU *c) {" % entry, "  uint8_t *const m = MEM; (void)m;"]
        out.extend(f.decls())
        if f.usize:
            out.append("  _Alignas(16) uint8_t ubuf[%d];" % f.usize)
        for n, ty in sorted(f.uniq.items()):
            out.append("  %s %s;" % (ty, n))
        if body and body[0] != entry:
            # body holds code below the entry (jumps back into a parent function): start at the entry
            need.add(entry)
            out.append("  goto L_%x;" % entry)
            stats["entry_not_first"] += 1
        for kind, x in lines:
            if kind == "L":
                if x in need:
                    out.append("L_%x: ;" % x)
            elif kind == "PL":
                out.append("%s: ;" % x)
            else:
                out.append("  " + x)
        out.append("}")
        files[fi // 120].append("\n".join(out))
        funcs_out.append(entry)

    hdr = ["#pragma once", '#include "cpu.h"']
    for e in funcs_out:
        hdr.append("void F_%x(CPU *c);" % e)
        hdr.append("void ORIG_%x(CPU *c);" % e)
    with open(os.path.join(outdir, "funcs.h"), "w") as fh:
        fh.write("\n".join(hdr) + "\n")
    for k, chunks in files.items():
        with open(os.path.join(outdir, "f%03d.c" % k), "w") as fh:
            fh.write('#include "funcs.h"\n\n')
            fh.write("\n\n".join(chunks))
            fh.write("\n")
    # Every function is reached through F_<addr> (direct calls, tail calls and rt_fntab). These weak
    # forwarders to the recompiled original ORIG_<addr> are overridden by rewritten functions (strong F_<addr>
    # in src/), which can still reach the original for comparison.
    with open(os.path.join(outdir, "weak.c"), "w") as fh:
        fh.write('#include "funcs.h"\n')
        for e in funcs_out:
            fh.write("__attribute__((weak)) void F_%x(CPU *c) { ORIG_%x(c); }\n" % (e, e))
    with open(os.path.join(outdir, "functab.c"), "w") as fh:
        fh.write('#include "funcs.h"\n')
        fh.write("const FnEnt rt_fntab[] = {\n")
        for e in funcs_out:
            fh.write("  {0x%xu, F_%x},\n" % (e, e))
        fh.write("};\nconst unsigned rt_nfn = %d;\n" % len(funcs_out))
    print(dict(stats), file=sys.stderr)


FLAGS = (0x200, 0x202, 0x204, 0x206, 0x207, 0x20b)
ALLF = frozenset(FLAGS)

def flag_of(v):
    """flag byte offset for a 1-byte flag varnode, 'other' for any other access touching the flag area."""
    if v is None or v[0] != "r":
        return None
    off, sz = v[1], v[2]
    if sz == 1 and off in FLAGS:
        return off
    if off < 0x216 and off + sz > 0x200:
        return "other"
    return None

def dead_flag_writes(body, bodyset, insns, entry, entryset, successors):
    """Backward liveness of the arithmetic flags over the function body. Returns {(addr, opindex)} of
    flag writes whose value is never read. Conservative: all flags live wherever control leaves the
    function (call, return, indirect/tail jump) and at instructions with internal p-code branches."""
    info = {}
    for a in body:
        ins = insns[a]
        ops = ins.ops
        exits = False
        internal = False
        for (opc, out, inp, oth) in ops:
            if opc in ("RETURN", "BRANCHIND", "CALLOTHER"):
                exits = True
            if opc in ("BRANCH", "CBRANCH"):
                t = inp[0]
                if t[0] == "c":
                    internal = True
                elif t[0] == "m" and (t[1] not in bodyset or (t[1] != entry and t[1] in entryset)):
                    exits = True
        succ = [x for x in successors(ins) if x in bodyset and not (x != entry and x in entryset)]
        if any(x not in bodyset or (x != entry and x in entryset) for x in successors(ins)):
            exits = True
        info[a] = (succ, exits, internal)
    live_in = {a: frozenset() for a in body}

    def transfer(a, live_out, record=None):
        ins = insns[a]
        succ, exits, internal = info[a]
        live = set(live_out)
        if exits:
            live |= ALLF
        if internal:
            for (opc, out, inp, oth) in ins.ops:
                for v in inp:
                    f = flag_of(v)
                    if f == "other": live |= ALLF
                    elif f is not None: live.add(f)
            return frozenset(live)
        for i in range(len(ins.ops) - 1, -1, -1):
            opc, out, inp, oth = ins.ops[i]
            if opc in ("CALL", "CALLIND"):
                # compiled code never passes flags into a call; the callee defines them
                live = set()
                for v in inp:
                    g = flag_of(v)
                    if g == "other": live |= ALLF
                    elif g is not None: live.add(g)
                continue
            f = flag_of(out)
            if f == "other":
                live |= ALLF
            elif f is not None:
                if f not in live and record is not None:
                    record.add((a, i))
                live.discard(f)
            for v in inp:
                g = flag_of(v)
                if g == "other": live |= ALLF
                elif g is not None: live.add(g)
        return frozenset(live)

    order = list(reversed(body))
    changed = True
    while changed:
        changed = False
        for a in order:
            lo = frozenset().union(*[live_in[x] for x in info[a][0]]) if info[a][0] else frozenset()
            li = transfer(a, lo)
            if li != live_in[a]:
                live_in[a] = li
                changed = True
    dead = set()
    for a in body:
        lo = frozenset().union(*[live_in[x] for x in info[a][0]]) if info[a][0] else frozenset()
        transfer(a, lo, dead)
    return dead


def dirty_sets(f, body, bodyset, insns, entry, entryset, successors):
    """Forward dataflow: cached register units possibly modified since the last sync (function entry or
    return from a call), at the start of each instruction."""
    def transfer(a, din):
        ins = insns[a]
        cur = set(din)
        internal = any(opc in ("BRANCH", "CBRANCH") and i_[0][0] == "c" for (opc, o, i_, ot) in ins.ops)
        for (opc, out, inp, oth) in ins.ops:
            if opc in ("CALL", "CALLIND") and not internal:
                cur = set()
            cur.update(f.units_written(out))
        return cur
    preds = {a: [] for a in body}
    for a in body:
        for x in successors(insns[a]):
            if x in preds and not (x != entry and x in entryset):
                preds[x].append(a)
    din = {a: set() for a in body}
    dout = {}
    work = list(body)
    inq = set(body)
    while work:
        a = work.pop()
        inq.discard(a)
        new_in = set().union(*[dout.get(p, set()) for p in preds[a]]) if preds[a] else set()
        if a in dout and new_in == din[a]:
            continue
        din[a] = new_in
        o = transfer(a, new_in)
        if dout.get(a) != o:
            dout[a] = o
            for x in successors(insns[a]):
                if x in preds and x not in inq and not (x != entry and x in entryset):
                    work.append(x); inq.add(x)
    return din


def fall_code(nxt, bodyset, entry, entryset, insns, need, stats, f):
    if nxt in bodyset and not (nxt != entry and nxt in entryset):
        need.add(nxt)
        return "goto L_%x;" % nxt
    if nxt in entryset:
        stats["fall_into_fn"] += 1
        return "%sF_%x(c); return;" % (f.sync_out(), nxt)
    stats["fall_missing"] += 1
    return "%srt_nofall(c, 0x%xu); return;" % (f.sync_out(), nxt)


def emit_ops(f, ins, lines, target_code, entryset, insns, stats, bodyset, dead=frozenset()):
    a = ins.addr
    nxt = a + ins.len
    ops = ins.ops
    f.plan_uniques(ins)
    # intra-instruction labels
    plabels = set()
    for i, (opc, out, inp, oth) in enumerate(ops):
        if opc in ("BRANCH", "CBRANCH") and inp[0][0] == "c":
            rel = inp[0][1] if inp[0][1] < 0x80000000 else inp[0][1] - (1 << 32)
            if i + rel < len(ops):
                plabels.add(i + rel)

    def xfer(t, i):
        sp, off, sz = t
        if sp == "c":
            rel = off if off < 0x80000000 else off - (1 << 32)
            j = i + rel
            if j >= len(ops):
                return fallx()
            return "goto P_%x_%d;" % (a, j)
        if sp == "m":
            return target_code(off)
        raise ValueError(t)

    def fallx():
        # branch to the next instruction from inside this one
        return target_code(nxt)

    for i, (opc, out, inp, oth) in enumerate(ops):
        if i in plabels:
            lines.append(("PL", "P_%x_%d" % (a, i)))
        r = f.rd
        if (a, i) in dead:
            continue
        if plabels and f.cache:
            f.dirty = None  # internal p-code branches: be conservative inside this instruction
        if opc == "COPY":
            lines.append(("C", f.wr(out, cast(out, r(inp[0]), inp[0]))))
        elif opc == "LOAD":
            sz = out[2]
            addr = r(inp[1])
            if sz == 10:
                e = "ld_f80(m, (uint32_t)(%s))" % addr
            else:
                e = "M%d((uint32_t)(%s))" % (sz, addr)
            lines.append(("C", f.wr(out, e)))
        elif opc == "STORE":
            sz = inp[2][2]
            addr = r(inp[1])
            if sz == 10:
                lines.append(("C", "st_f80(m, (uint32_t)(%s), %s);" % (addr, r(inp[2]))))
            else:
                lines.append(("C", "M%d((uint32_t)(%s)) = %s;" % (sz, addr, r(inp[2]))))
        elif opc == "BRANCH":
            lines.append(("C", xfer(inp[0], i)))
        elif opc == "CBRANCH":
            lines.append(("C", "if (%s) %s" % (r(inp[1]), xfer(inp[0], i))))
        elif opc == "BRANCHIND":
            t = r(inp[0])
            cases = []
            for fl in sorted(set(ins.flows)):
                cases.append("case 0x%xu: %s" % (fl, f_target(fl, target_code)))
            if cases:
                stats["jumptable"] += 1
                lines.append(("C", "{ uint32_t jt_ = %s; switch (jt_) { %s default: %srt_jump(c, jt_); return; } }" % (t, " ".join(cases), f.sync_out())))
            else:
                stats["jmp_ind"] += 1
                lines.append(("C", "{ uint32_t jt_ = %s; %srt_jump(c, jt_); return; }" % (t, f.sync_out())))
        elif opc == "CALL":
            t = inp[0]
            if t[0] == "m" and t[1] in entryset:
                lines.append(("C", "%sF_%x(c); RETCHK(0x%xu);%s" % (f.sync_out(), t[1], nxt, f.sync_in())))
            else:
                lines.append(("C", "{ uint32_t ct_ = %s; %srt_call(c, ct_); RETCHK(0x%xu);%s }" % (lit(t[1], 4) if t[0] == "m" else r(t), f.sync_out(), nxt, f.sync_in())))
            if f.cache and f.dirty is not None: f.dirty = set()
        elif opc == "CALLIND":
            lines.append(("C", "{ uint32_t ct_ = %s; %srt_call(c, ct_); RETCHK(0x%xu);%s }" % (r(inp[0]), f.sync_out(), nxt, f.sync_in())))
            if f.cache and f.dirty is not None: f.dirty = set()
        elif opc == "RETURN":
            lines.append(("C", "{ uint32_t rt_ = %s; %sc->ret = rt_; return; }" % (r(inp[0]), f.sync_out())))
        elif opc == "CALLOTHER":
            if oth in ("LOCK", "UNLOCK"):
                continue
            raise ValueError("callother %s at %x" % (oth, a))
        else:
            lines.append(("C", f.wr(out, expr(opc, out, inp, r))))
        if f.cache and f.dirty is not None and opc not in ("CALL", "CALLIND"):
            f.dirty.update(f.units_written(out))
    if plabels and f.cache:
        f.dirty = None
    # label at end of pcode (branch to len(ops)) is handled by fallx()


def f_target(fl, target_code):
    return target_code(fl)


def cast(out, e, src):
    return "(%s)(%s)" % (T[out[2]], e) if out[2] != src[2] else e


def expr(opc, out, inp, r):
    osz = out[2]
    ot = T[osz]
    a = r(inp[0])
    asz = inp[0][2]
    at, ast = T[asz], S[asz]
    b = r(inp[1]) if len(inp) > 1 else None
    bsz = inp[1][2] if len(inp) > 1 else None
    bits = BITS[asz]
    W = "uint64_t" if asz == 8 else ("u128" if asz == 16 else "uint32_t")  # work type for <=4: uint32
    if opc == "INT_ADD":
        return "(%s)((%s)%s + (%s)%s)" % (ot, W, a, W, b)
    if opc == "INT_SUB":
        return "(%s)((%s)%s - (%s)%s)" % (ot, W, a, W, b)
    if opc == "INT_MULT":
        return "(%s)((%s)%s * (%s)%s)" % (ot, W, a, W, b)
    if opc == "INT_AND":
        return "(%s)(%s & %s)" % (ot, a, b)
    if opc == "INT_OR":
        return "(%s)(%s | %s)" % (ot, a, b)
    if opc == "INT_XOR":
        return "(%s)(%s ^ %s)" % (ot, a, b)
    if opc == "INT_NEGATE":
        return "(%s)~(%s)%s" % (ot, W, a)
    if opc == "INT_2COMP":
        return "(%s)(0 - (%s)%s)" % (ot, W, a)
    if opc == "INT_LEFT":
        return "(%s)shl%d(%s, (uint64_t)%s)" % (ot, asz, a, b)
    if opc == "INT_RIGHT":
        return "(%s)shr%d(%s, (uint64_t)%s)" % (ot, asz, a, b)
    if opc == "INT_SRIGHT":
        return "(%s)sar%d(%s, (uint64_t)%s)" % (ot, asz, a, b)
    if opc == "INT_DIV":
        return "(%s)udiv%d(%s, %s)" % (ot, asz, a, b)
    if opc == "INT_REM":
        return "(%s)urem%d(%s, %s)" % (ot, asz, a, b)
    if opc == "INT_SDIV":
        return "(%s)sdiv%d(%s, %s)" % (ot, asz, a, b)
    if opc == "INT_SREM":
        return "(%s)srem%d(%s, %s)" % (ot, asz, a, b)
    if opc == "INT_EQUAL":
        return "(uint8_t)(%s == %s)" % (a, b)
    if opc == "INT_NOTEQUAL":
        return "(uint8_t)(%s != %s)" % (a, b)
    if opc == "INT_LESS":
        return "(uint8_t)((%s)%s < (%s)%s)" % (at, a, at, b)
    if opc == "INT_LESSEQUAL":
        return "(uint8_t)((%s)%s <= (%s)%s)" % (at, a, at, b)
    if opc == "INT_SLESS":
        return "(uint8_t)((%s)%s < (%s)%s)" % (ast, a, ast, b)
    if opc == "INT_SLESSEQUAL":
        return "(uint8_t)((%s)%s <= (%s)%s)" % (ast, a, ast, b)
    if opc == "INT_CARRY":
        return "(uint8_t)((%s)((%s)%s + (%s)%s) < (%s)%s)" % (at, W, a, W, b, at, a)
    if opc == "INT_SCARRY":
        return "scarry%d(%s, %s)" % (asz, a, b)
    if opc == "INT_SBORROW":
        return "sborrow%d(%s, %s)" % (asz, a, b)
    if opc == "INT_ZEXT":
        return "(%s)%s" % (ot, a)
    if opc == "INT_SEXT":
        return "(%s)(%s)(%s)%s" % (ot, S[osz], ast, a)
    if opc == "POPCOUNT":
        return "(%s)popc%d(%s)" % (ot, asz, a)
    if opc == "SUBPIECE":
        k = inp[1][1]
        return "(%s)(%s >> %d)" % (ot, a, 8 * k) if k else "(%s)%s" % (ot, a)
    if opc == "PIECE":
        return "(%s)(((%s)%s << %d) | (%s)%s)" % (ot, ot, a, 8 * bsz, ot, b)
    if opc == "BOOL_NEGATE":
        return "(uint8_t)!%s" % a
    if opc == "BOOL_AND":
        return "(uint8_t)(%s & %s)" % (a, b)
    if opc == "BOOL_OR":
        return "(uint8_t)(%s | %s)" % (a, b)
    if opc == "BOOL_XOR":
        return "(uint8_t)(%s ^ %s)" % (a, b)
    # floats: size 4 = float, 8/10 = double
    fk = "f4" if asz == 4 else "f8"
    if opc in ("FLOAT_ADD", "FLOAT_SUB", "FLOAT_MULT", "FLOAT_DIV"):
        o = {"FLOAT_ADD": "+", "FLOAT_SUB": "-", "FLOAT_MULT": "*", "FLOAT_DIV": "/"}[opc]
        bk = "b4" if osz == 4 else "b8"
        return "%s(%s(%s) %s %s(%s))" % (bk, fk, a, o, fk, b)
    if opc == "FLOAT_NEG":
        return "%s(-%s(%s))" % ("b4" if osz == 4 else "b8", fk, a)
    if opc == "FLOAT_ABS":
        return "%s(fabs_%s(%s(%s)))" % ("b4" if osz == 4 else "b8", fk, fk, a)
    if opc == "FLOAT_SQRT":
        return "%s(sqrt_%s(%s(%s)))" % ("b4" if osz == 4 else "b8", fk, fk, a)
    if opc == "FLOAT_NAN":
        return "(uint8_t)(%s(%s) != %s(%s))" % (fk, a, fk, a)
    if opc == "FLOAT_EQUAL":
        return "(uint8_t)(%s(%s) == %s(%s))" % (fk, a, fk, b)
    if opc == "FLOAT_NOTEQUAL":
        return "(uint8_t)(%s(%s) != %s(%s))" % (fk, a, fk, b)
    if opc == "FLOAT_LESS":
        return "(uint8_t)(%s(%s) < %s(%s))" % (fk, a, fk, b)
    if opc == "FLOAT_LESSEQUAL":
        return "(uint8_t)(%s(%s) <= %s(%s))" % (fk, a, fk, b)
    if opc == "INT2FLOAT":
        return "%s((%s)(%s)%s)" % ("b4" if osz == 4 else "b8", "float" if osz == 4 else "double", ast, a)
    if opc == "FLOAT2FLOAT":
        if osz == 4:
            return "b4((float)%s(%s))" % (fk, a)
        return "b8((double)%s(%s))" % (fk, a)
    if opc == "TRUNC":
        return "(%s)trunc_%s_%d(%s(%s))" % (ot, fk, osz, fk, a)
    raise ValueError("unhandled op " + opc)


if __name__ == "__main__":
    main()
