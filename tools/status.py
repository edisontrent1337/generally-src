#!/usr/bin/env python3
"""Progress of the rewrite: game functions (analysis/functions.tsv) vs REWRITE(...) entries in src/.

usage: tools/status.py [-v]      -v also lists the rewritten functions
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def main():
    game = {}
    for line in open(os.path.join(ROOT, "analysis/functions.tsv")):
        if line.startswith("addr"):
            continue
        a, size, cls, label = line.rstrip("\n").split("\t")[:4]
        if cls == "game":
            game[int(a, 16)] = (int(size), label)
    done = {}
    for dp, _, fs in os.walk(os.path.join(ROOT, "src")):
        for f in fs:
            if f.endswith(".cpp"):
                for m in re.finditer(r'^REWRITE\(([0-9a-f]+),\s*"([^"]*)"', open(os.path.join(dp, f)).read(), re.M):
                    done[int(m.group(1), 16)] = (m.group(2), os.path.relpath(os.path.join(dp, f), ROOT))
    total = sum(s for s, _ in game.values())
    dsz = sum(game[a][0] for a in done if a in game)
    print("game functions rewritten: %d / %d   (%.1f%% of game code by size: %d / %d bytes)" % (
        len([a for a in done if a in game]), len(game), 100.0 * dsz / total, dsz, total))
    other = [a for a in done if a not in game]
    if other:
        print("also rewritten (not classified as game code): %s" % " ".join("%x" % a for a in other))
    if "-v" in sys.argv:
        for a in sorted(done):
            print("  %08x %-40s %s" % (a, done[a][0], done[a][1]))


if __name__ == "__main__":
    main()
