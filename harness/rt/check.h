// Comparison harness: hooks for rewritten functions, shadow execution, per-frame trace.
//
// Every rewritten function overrides the weak F_<addr> of the recompiled original and enters through
// chk_dispatch(), which can
//   - run the recompiled original instead       GRX_ORIG=all | addr,addr,...   (bisecting)
//   - run both and compare their effects        GRX_SHADOW=all | addr,...      (GRX_SHADOW_N=max checks/fn)
// Shadow execution forks: the child runs the original, the parent the rewrite; both hash every page of guest
// memory (image, runtime data, stacks above the call frame, heap) and the result registers, and the parent
// reports any difference down to the byte. A child about to touch the outside world (files, window, audio)
// bails out instead (chk_extern): that call is only covered by the whole-run traces; later calls are still
// checked (until 200 calls of the function have bailed).
#pragma once
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

// RET_AL: a C++ bool; MSVC sets only AL and leaves the rest of EAX as it happens to be, callers test AL
enum { RET_NONE, RET_EAX, RET_EDXEAX, RET_ST0, RET_XMM0, RET_AL };

typedef struct Hook {
    uint32_t addr;
    const char *name;
    void (*orig)(CPU *);   // recompiled original
    void (*repl)(CPU *);   // adapter: reads guest args, calls the rewrite, stores the result registers
    uint16_t args;         // stack argument dwords (excluded from the shadow comparison: dead after return)
    uint16_t pop;          // bytes the function pops on return (`ret N`): 0 cdecl, 4*args stdcall/thiscall
    uint8_t ret;           // RET_*: which result registers are compared
    // runtime state
    uint8_t use_orig, shadow, unshadowable;
    uint32_t calls, checks, mismatches;
    struct Hook *next;
} Hook;

void chk_register(Hook *h);
void chk_dispatch(Hook *h, CPU *c);
// shims with effects outside guest memory call this first; inside a shadow child it abandons the check
void chk_extern(const char *what);
int chk_pure_import(const char *name);   // may a shadow child call this import?
extern int chk_in_shadow_child;

// deterministic-mode frame events (shim_sdl.c)
extern uint32_t chk_frame;              // frames presented so far
void chk_frame_end(const uint8_t *pixels, int w, int h, int pitch, const uint8_t *palette_rgba);
void chk_audio_hash(const uint8_t *buf, uint32_t len);
void chk_exit(int code);                // flush trace + stats, exit
void chk_init(void);

// thread's current CPU (set on entry to rewritten code, used for calls back into guest code)
extern _Thread_local CPU *chk_cpu;

#ifdef __cplusplus
}
#endif
