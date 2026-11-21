#!/bin/sh
# tools/show.sh ADDR...   Ghidra pseudo-C + calling convention summary of original functions
cd "$(dirname "$0")/.."
for a in "$@"; do
  a=$(printf '%08x' 0x$a)
  awk -v a="@ $a" 'index($0,"//==== ")==1{p=(index($0,a)>0)} p' build/decomp.c | grep -v '^$'
  build/venv/bin/python -I tools/disasm.py $a 2>/dev/null | grep -E 'ret pops|stack refs'
  printf 'callers: '; awk -F'\t' -v a=$a '$1==a{print $5}' build/funcinfo.tsv
done
