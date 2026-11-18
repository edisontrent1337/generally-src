# GeneRally 1.2d — readable source reconstruction

Goal: a complete, human-readable C++ source of GeneRally 1.2d (2018-01-06, `GeneRally.exe`
sha256 `868175b2…82391`) that behaves exactly like the original.

The original is a 32-bit MSVC 2015 C++ program (SDL 1.2, no debug info). This repository rebuilds it
function by function, and every rewritten function is checked against the original *while the game runs*.

## How it works

1. **The original, recompiled.** Ghidra exports the original exe's machine code as p-code once
   (`make lift`); `tools/lift.py` turns every function into portable C (`build/lift/`, generated, not
   committed). With the runtime in `harness/rt/` (guest memory, Windows/CRT/MSVCP/SDL 1.2 shims) this
   runs the unmodified game natively on macOS (`build/gr_orig`).
2. **Rewritten functions replace recompiled ones.** Each recompiled function `ORIG_xxxxxx` is reached
   through a weak symbol `F_xxxxxx`. A function rewritten in `src/` defines the strong `F_xxxxxx`, so
   every caller — direct calls, vtables, callbacks — now runs the rewrite (`build/gr`). The rewrite works
   on the game's own memory: the guest address space sits at a fixed host address, so game data is
   accessed through ordinary typed structs (`src/bridge/guest.h`).
3. **Checking.** `tools/check.py` runs scripted, fully deterministic sessions (virtual clock, scripted
   input, synchronous audio) of the original and the rewrite and requires identical per-frame traces
   (screen + palette, game memory, audio, heap) and identical files written. With shadow checking, every
   call of a rewritten function is additionally run both ways (fork: original in the child, rewrite in the
   parent) and all guest memory and result registers are compared, reporting exact byte differences.

When every game function is rewritten, the recompiled original and the bridge are no longer needed:
`ptr32<T>` becomes `T*`, the MSVC library mirrors become the standard library, and the source stands on
its own.

## Layout

| path | what |
|---|---|
| `src/game/` | the rewritten game, readable C++ (this is the actual product) |
| `src/hooks/` | entry adapters: where original code calls a rewritten function (calling convention glue) |
| `src/orig/` | typed wrappers for calling original functions that are not rewritten yet |
| `src/bridge/guest.h` | guest memory, `ptr32<T>`, `GLOBAL`, `REWRITE`, calls into the original |
| `harness/rt/` | runtime for the recompiled original; `check.c` = hooks, shadow checks, traces |
| `tools/lift.py`, `tools/gen_image.py` | static recompiler (p-code → C), exe image → C |
| `tools/check.py` | comparison runner; `tests/scenarios/*.txt` = scripted sessions |
| `tools/classify.py` → `analysis/functions.tsv` | every function: game / stl / crt / eh / init / compiler |
| `tools/disasm.py` | disassembly + calling convention summary of original functions |
| `tools/status.py` | progress |
| `NOTES.md` | findings about the game (data layouts, conventions, globals) and the progress log |

The game files are third-party and not in the repository: put an unmodified GeneRally 1.2d install in
`game/orig/` (the sha256 of the exe is in `game/GeneRally.exe.sha256`).

## Build and check

```
make venv                 # python env for tools/disasm.py (capstone, pefile)
make lift                 # once: Ghidra export + recompile (needs Ghidra + JDK: brew install ghidra openjdk@25)
make decomp               # once: build/decomp.c (Ghidra pseudo-C) + build/funcinfo.tsv, reference material
make -j8                  # build/gr (with the rewrite) and build/gr_orig
python3 tools/check.py    # all scenarios, whole-run + shadow comparison
python3 tools/check.py --selftest   # harness sanity: the original twice must be identical
```

Play: `cd game/orig && ../../build/gr` (window, sound, keyboard; same runtime as the checks).

Runtime switches: `GRX_DET=1` deterministic clock, `GRX_SCRIPT=ms:click:x:y,ms:key:sym,ms:down:sym,ms:up:sym,ms:snap:n,ms:quit:0`,
`GRX_TRACE=file`, `GRX_ORIG=all|addr,...` (use the original for these), `GRX_SHADOW=all|addr,...`,
`GRX_SHADOW_N=n` (checks per function), `GRX_HEADLESS=1` with `SDL_VIDEODRIVER=dummy`.

## Rewriting a function

1. Pick one (`analysis/functions.tsv`, class `game`; leaves first). Read `build/decomp.c`
   (`awk '/@ 00405f10/,/^}/' build/decomp.c`) and the disassembly (`build/venv/bin/python -I tools/disasm.py 405f10`)
   for the real calling convention: `ret N`, register and XMM arguments (MSVC whole-program optimisation passes
   floats in the XMM register of their argument position).
2. Write it readably in `src/game/` with real types and names; record data layouts in `NOTES.md`.
3. Add the entry adapter in `src/hooks/`: `REWRITE(addr, "name", stack_arg_dwords, bytes_popped, RET_kind) {...}`.
4. If it calls functions that are not rewritten yet, call them through a typed wrapper in `src/orig/`.
5. `make -j8 && python3 tools/check.py`. A mismatch names the function, the call and the differing bytes;
   `--orig addr,...` bisects whole-run divergences.

Rules that keep results identical: same float precision and operation order (no FMA, float vs double as in the
original), same calls to the C library and SDL in the same order, same heap allocations (use the original's
allocation paths), never pass host memory to original code (`guest::Local<T>` for scratch on the guest stack).
