# Notes

Findings about GeneRally 1.2d needed for the rewrite, and the progress log. Addresses are in the original exe
(image base 0x400000). Earlier reverse engineering (12-car mod, netplay, macOS port) lives in the sibling repo
`generally-mod` (its NOTES.md documents many globals and per-car structures); the harness here was vendored
from it at the commit in `tools/VENDORED_FROM` and adapted to the unmodified exe.

## Code classes (analysis/functions.tsv)

328 game functions (~467 KB) to rewrite; 182 STL template instantiations (std::string, vector, fstream/filebuf,
stringstream, std::sort ×3, <cmath> float overloads), 144 statically linked CRT pieces, 573 exception-unwind
funclets, 9 global constructors/destructors, 7 other compiler-generated functions. The game links UCRT,
VCRUNTIME140 and MSVCP140 as DLLs.

## Calling conventions seen

- MSVC whole-program optimisation: `this` in ECX; float arguments in the XMM register of their *position*
  (Vec3(x,y,z) at 0x405ed0: xmm1..xmm3; Vec3::operator*(float) at 0x405fc0: out pointer on the stack, factor in
  xmm2); class results through a hidden pointer, returned in EAX.
- thiscall / stdcall pop their stack arguments (`ret N`), cdecl does not.

## Data layouts

- Vec3: `{float x, y, z}` (12 bytes), methods 0x405eb0..0x406070.

## Harness facts

- Deterministic mode: virtual clock starts at 1 s; +1 µs per clock read, +10 µs per event poll, Sleep/SDL_Delay
  advance by their argument, each SDL_Flip by 16.667 ms; the mixer runs synchronously at each flip.
  Boot reaches the main menu after ~13.4 s of virtual time (frame ~300).
- Main menu (640×480): driver list rows x=430, y=96+16k (clicking adds the driver; the list shifts up);
  track list first entry 116,288; Start 560,464; Options 240,464; Exit 80,464. The race starts after a key
  press on the info box; Player drives with the arrow keys (273 up, 274 down, 275 right, 276 left).
- The guest arena is at host 0x10000000000 (macOS refuses fixed mappings at some lower addresses).

## Progress log

- 2026-11-18: repository set up; recompiled original runs deterministically (boot, menu, 5-car race:
  identical traces and files across runs). Vec3 (10 functions) rewritten: identical traces over a 40 s race
  (~650k calls), shadow-checked; a planted bug is caught by both the trace and the shadow check.
