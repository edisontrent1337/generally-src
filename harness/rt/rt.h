// Runtime internals shared by the shim modules.
#pragma once
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>

// ---- guest address map
#define IMAGE_BASE   0x00400000u
#define TEB_AREA     0x00600000u   // per-thread TEB/TLS blocks (4 KB each, 64 threads)
#define DATA_AREA    0x00640000u   // data imports and other runtime-owned guest variables
#define THUNK_BASE   0x00680000u   // import thunks: THUNK_BASE + 16*i (never executed)
#define THUNK_MAX    1024
#define MAGIC_RET    0x006ffff0u   // return address used when the host calls guest code
#define STACK_AREA   0x01000000u   // thread stacks
#define MAIN_STACK   (16u << 20)
#define THREAD_STACK (2u << 20)
#define HEAP_BASE    0x10000000u
#define HEAP_END     0xf0000000u

#define G8(a)  (*(uint8_t *)(MEM + (uint32_t)(a)))
#define G16(a) (*(u16u *)(MEM + (uint32_t)(a)))
#define G32(a) (*(u32u *)(MEM + (uint32_t)(a)))
#define G64(a) (*(u64u *)(MEM + (uint32_t)(a)))
#define GP(a)  ((void *)(MEM + (uint32_t)(a)))
#define GS(a)  ((char *)(MEM + (uint32_t)(a)))   // guest C string
#define GADDR(p) ((uint32_t)((uint8_t *)(p) - MEM))

// ---- shim calling helpers. Shims are entered with ESP pointing at the return address.
#define ARG(i) G32(ESP + 4 + 4 * (i))
#define ARGD(i) f8(G64(ESP + 4 + 4 * (i)))  // double occupying ARG(i), ARG(i+1)
static inline void ret_c(CPU *c, uint32_t v) { EAX = v; c->ret = G32(ESP); ESP += 4; }
static inline void ret_std(CPU *c, uint32_t v, int nargs) { EAX = v; c->ret = G32(ESP); ESP += 4 + 4 * nargs; }
static inline void ret_c64(CPU *c, uint64_t v) { EAX = (uint32_t)v; EDX = (uint32_t)(v >> 32); c->ret = G32(ESP); ESP += 4; }
void fpu_push(CPU *c, double v);   // return a double in ST0 (cdecl float return)
void fpu_pop(CPU *c);              // drop ST0 (after reading a float result)

typedef void (*ShimFn)(CPU *c);
typedef struct { const char *name; ShimFn fn; } ShimDef;
// each shim module exports a NULL-terminated table
extern const ShimDef shims_k32[], shims_crt[], shims_msvcp[], shims_sdl[];

// ---- services
_Noreturn void rt_fatal(CPU *c, const char *fmt, ...);
void rt_log(const char *fmt, ...);
uint32_t rt_thunk_for(const char *name);          // thunk address for a named shim (0 if unknown)
uint32_t guest_call(CPU *c, uint32_t fn, int nargs, const uint32_t *args);  // any convention
uint32_t guest_thiscall(CPU *c, uint32_t fn, uint32_t self, int nargs, const uint32_t *args);
CPU *rt_new_cpu(uint32_t stack_size);               // CPU with stack + TEB, for a new host thread
void rt_free_cpu(CPU *c);

uint32_t gmalloc(uint32_t size);
void gfree(uint32_t p);
uint32_t grealloc(uint32_t p, uint32_t size);
uint32_t gcalloc(uint32_t n, uint32_t size);
uint32_t gstrdup(const char *s);
uint32_t gdata_alloc(uint32_t size);                 // permanent runtime data in DATA_AREA

// host path from a guest (Windows-style) path
void host_path(const char *gpath, char *out, size_t outsz);

extern int rt_quit_requested;
