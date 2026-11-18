#!/usr/bin/env python3
"""Does the rewrite behave exactly like the original?

For every scenario (tests/scenarios/*.txt) the recompiled original (build/gr_orig) and the build with the
rewritten functions (build/gr) are run headless and deterministic (GRX_DET=1) with the same scripted input,
each in a fresh copy of the game folder. The per-frame traces (screen + palette, game image memory, audio,
heap) and every file the game wrote must be identical.

usage: tools/check.py [scenario ...] [--shadow all|addr,...] [--orig addr,...] [--selftest] [--keep]
  --shadow   also compare each rewritten function against the original on every call (fork-based, see
             harness/rt/check.h); default: all rewritten functions, first 30 calls each (--shadow-n)
  --no-shadow  whole-run comparison only
  --orig     forward these rewritten functions to the original (bisecting a divergence)
  --selftest run the original twice and require identical traces (harness determinism)
"""
import argparse, concurrent.futures as cf, filecmp, os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GAME = os.path.join(ROOT, "game/orig")
OUT = os.path.join(ROOT, "build/check")
SKIP_FILES = {"trace.txt", "grx.log", "stdout.txt"}


def scenarios():
    d = os.path.join(ROOT, "tests/scenarios")
    out = {}
    for f in sorted(os.listdir(d)):
        if f.endswith(".txt"):
            meta = {"about": []}
            for line in open(os.path.join(d, f)):
                line = line.rstrip("\n")
                if line.startswith("#"):
                    meta["about"].append(line[1:].strip())
                elif ":" in line:
                    k, v = line.split(":", 1)
                    meta[k.strip()] = v.strip()
            out[f[:-4]] = meta
    return out


def run(binary, scen, tag, env_extra, timeout):
    d = os.path.join(OUT, scen["name"], tag)
    shutil.rmtree(d, ignore_errors=True)
    shutil.copytree(GAME, d)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", GRX_HEADLESS="1", GRX_DET="1",
               GRX_TRACE="trace.txt", GRX_SCRIPT=scen["script"], GRX_TRACE_HEAP=scen.get("heap_every", "30"))
    env.update(env_extra)
    with open(os.path.join(d, "stdout.txt"), "w") as so:
        try:
            p = subprocess.run([binary], cwd=d, env=env, stdout=so, stderr=subprocess.STDOUT, timeout=timeout)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
    return d, rc


def read_trace(d):
    p = os.path.join(d, "trace.txt")
    return open(p).read().splitlines() if os.path.exists(p) else []


def fields(line):
    out = {}
    for tok in line.split()[1:]:
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def compare_traces(a, b):
    """-> None if identical, else description of the first difference."""
    for i in range(max(len(a), len(b))):
        la = a[i] if i < len(a) else "<missing>"
        lb = b[i] if i < len(b) else "<missing>"
        if la != lb:
            fa, fb = fields(la), fields(lb)
            diff = [k for k in sorted(set(fa) | set(fb)) if fa.get(k) != fb.get(k)]
            return "line %d (%s): differs in %s\n    original: %s\n    rewrite:  %s" % (
                i + 1, la.split()[1] if la.startswith("F ") else "", ",".join(diff) or "?", la, lb)
    return None


def compare_files(da, db):
    diffs = []

    def walk(rel):
        cmp = filecmp.dircmp(os.path.join(da, rel), os.path.join(db, rel))
        for n in cmp.left_only + cmp.right_only:
            if n not in SKIP_FILES:
                diffs.append("only in one run: " + os.path.join(rel, n))
        for n in cmp.diff_files:
            if n not in SKIP_FILES:
                diffs.append("contents differ: " + os.path.join(rel, n))
        for n in cmp.common_files:   # dircmp is shallow (size/mtime): confirm byte equality
            if n not in SKIP_FILES and n not in cmp.diff_files and \
               not filecmp.cmp(os.path.join(da, rel, n), os.path.join(db, rel, n), shallow=False):
                diffs.append("contents differ: " + os.path.join(rel, n))
        for s in cmp.common_dirs:
            walk(os.path.join(rel, s))

    walk("")
    return diffs


def shadow_report(d):
    log = os.path.join(d, "grx.log")
    if not os.path.exists(log):
        return [], []
    lines = open(log, errors="replace").read().splitlines()
    mism = [l for l in lines if l.startswith("SHADOW MISMATCH") or l.startswith("    ")
            or ("shadow" in l and "crashed" in l)]
    summary = [l for l in lines if l.startswith("shadow ") and "calls" in l]
    return mism, summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*")
    ap.add_argument("--shadow", default="all")
    ap.add_argument("--no-shadow", action="store_true")
    ap.add_argument("--shadow-n", default="30")
    ap.add_argument("--orig", default="")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--timeout", type=int, default=900)
    a = ap.parse_args()
    sc = scenarios()
    names = a.names or list(sc)
    for n in names:
        if n not in sc:
            sys.exit("unknown scenario %s (have: %s)" % (n, ", ".join(sc)))
        sc[n]["name"] = n
    ref_bin = os.path.join(ROOT, "build/gr_orig")
    new_bin = ref_bin if a.selftest else os.path.join(ROOT, "build/gr")
    new_env = {}
    if not a.selftest:
        if a.orig:
            new_env["GRX_ORIG"] = a.orig
        if not a.no_shadow:
            new_env["GRX_SHADOW"] = a.shadow
            new_env["GRX_SHADOW_N"] = a.shadow_n
    ok = True
    with cf.ThreadPoolExecutor(max_workers=2 * len(names)) as ex:
        jobs = {}
        for n in names:
            jobs[n] = (ex.submit(run, ref_bin, sc[n], "original", {}, a.timeout),
                       ex.submit(run, new_bin, sc[n], "rewrite", new_env, a.timeout))
        for n in names:
            (da, ra), (db, rb) = jobs[n][0].result(), jobs[n][1].result()
            ta, tb = read_trace(da), read_trace(db)
            problems = []
            if ra != 0 or rb != 0:
                problems.append("exit status original=%s rewrite=%s (see %s/stdout.txt)" % (ra, rb, db))
            d = compare_traces(ta, tb)
            if d:
                problems.append("trace " + d)
            problems += compare_files(da, db)
            mism, summary = shadow_report(db)
            frames = ta[-1] if ta else "<no trace>"
            print("%-12s %s  %s" % (n, "OK  " if not problems and not mism else "FAIL", frames))
            for p in problems:
                print("    " + p)
            for l in mism[:40]:
                print("    " + l)
            if summary and os.environ.get("GRX_VERBOSE"):
                for l in summary:
                    print("    " + l)
            checked = sum(1 for l in summary if " checked " in l and " checked 0 " not in l)
            if summary:
                print("    shadow: %d functions compared call by call, %d not checkable" % (
                    checked, sum(1 for l in summary if "not checkable" in l)))
            ok = ok and not problems and not mism
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
