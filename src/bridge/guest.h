// Bridge between the rewritten source and the recompiled original it runs inside of.
//
// The rewrite is linked into the same binary as the recompiled original (build/lift). Guest memory (the
// original's address space: its globals, heap and stacks) is mapped at ARENA_BASE, so guest address A is
// host address ARENA_BASE + A. Game data therefore lives where the original expects it, and rewritten code
// reads and writes it directly through typed structs.
//
// While the two halves coexist:
//   - pointers stored *inside* game data are 32-bit guest addresses: use ptr32<T>, which behaves like T*;
//   - globals are declared with GLOBAL(type, name, address);
//   - rewritten functions are entered from original code through an adapter (REWRITE, see below);
//   - not yet rewritten functions are called through guest::call_* (src/orig/*.h wraps them in typed
//     functions, so game code just calls a C++ function either way).
// Once everything is rewritten this header goes away: ptr32<T> becomes T*, GLOBAL a plain definition.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "bridge/imports.h"

extern "C" {
#include "check.h"
#include "cpu.h"
#include "funcs.h"
#include "plat.h"
#include "rt.h"
void fpu_pop(CPU *c);
}

namespace guest {

constexpr uintptr_t kBase = ARENA_BASE;

inline void *host(uint32_t a) { return reinterpret_cast<void *>(kBase + a); }
template <class T> inline T &at(uint32_t a) { return *reinterpret_cast<T *>(kBase + a); }

// Guest address of a host pointer. Rewritten code must only hand guest memory to original code (never a
// pointer to a host local or a host heap block), so anything else is a bug and stops the program.
inline uint32_t addr(const void *p) {
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    if (!p) return 0;
    if (v - kBase >= (1ull << 32)) rt_fatal(chk_cpu, "host pointer %p passed to guest code", p);
    return static_cast<uint32_t>(v - kBase);
}

inline CPU *cpu() { return chk_cpu; }

// float -> int as the x86 instruction does it (cvttss2si): truncate; NaN and out of range give INT32_MIN.
// (A plain C++ cast is undefined there and saturates on ARM.)
inline int32_t to_int(float x) { return trunc_f4_4(x); }
inline int32_t to_int(double x) { return trunc_f8_4(x); }
// the CRT float -> 64-bit integer helper (0x497df0, argument in xmm0): truncates; NaN and out-of-range
// values give INT64_MIN like cvttss2si with a 64-bit destination
inline int64_t to_int64(float x) {
    if (!(x > -9223372036854775808.0f && x < 9223372036854775808.0f)) return INT64_MIN;
    return static_cast<int64_t>(x);
}

// the CRT double -> 64-bit integer helper (0x497fd1, _dtol3): truncates; NaN and out-of-range values give
// INT64_MIN
inline int64_t to_int64(double x) {
    if (!(x > -9223372036854775808.0 && x < 9223372036854775808.0)) return INT64_MIN;
    return static_cast<int64_t>(x);
}

// operator new / sized operator delete of the original (its heap, so block addresses match)
inline void *op_new(uint32_t n);
inline void op_delete(void *p, uint32_t n);

}  // namespace guest

// A pointer stored in game data: 32 bits, converts to and from T*.
template <class T> struct ptr32 {
    uint32_t a;
    ptr32() = default;
    ptr32(std::nullptr_t) : a(0) {}
    ptr32(T *p) : a(guest::addr(p)) {}
    static ptr32 from_addr(uint32_t x) { ptr32 r; r.a = x; return r; }
    T *get() const { return a ? reinterpret_cast<T *>(guest::kBase + a) : nullptr; }
    operator T *() const { return get(); }
    T *operator->() const { return get(); }
    std::add_lvalue_reference_t<T> operator*() const { return *get(); }
    explicit operator bool() const { return a != 0; }
};
static_assert(sizeof(ptr32<int>) == 4 && std::is_trivially_copyable_v<ptr32<int>>);

template <class T> using gref = T &;
// GLOBAL(int, g_player_count, 0x4a4dc0);  GLOBAL(Car[6], g_cars, 0x4a5210);
#define GLOBAL(type, name, address) inline gref<type> name = guest::at<type>(address)

// ------------------------------------------------------------------ adapters (original -> rewrite)
// Entered with ESP at the return address. Read arguments, call the rewritten function, set the result
// register; chk_dispatch does the return (pops the return address and `pop` bytes of arguments).
namespace abi {
inline uint32_t arg(CPU *c, int i) { return G32(ESP + 4 + 4 * i); }
inline int32_t argi(CPU *c, int i) { return static_cast<int32_t>(arg(c, i)); }
inline float argf(CPU *c, int i) { uint32_t v = arg(c, i); float f; std::memcpy(&f, &v, 4); return f; }
template <class T> inline T *argp(CPU *c, int i) { uint32_t a = arg(c, i); return a ? &guest::at<T>(a) : nullptr; }
template <class T> inline T *ecx(CPU *c) { return ECX ? &guest::at<T>(ECX) : nullptr; }
template <class T> inline T *edx(CPU *c) { return EDX ? &guest::at<T>(EDX) : nullptr; }
inline float xmm(CPU *c, int n) { return XMM_F(n, 0); }
inline void ret(CPU *c, uint32_t v) { EAX = v; }
inline void ret_ptr(CPU *c, const void *p) { EAX = guest::addr(p); }
inline void ret_bool(CPU *c, bool b) { EAX = b; }  // with RET_AL
inline void ret_st0(CPU *c, double v) { fpu_push(c, v); }
inline void ret_xmm0(CPU *c, float v) { XMM_F(0, 0) = v; }
}  // namespace abi

// REWRITE(addr, "name", stack_arg_dwords, bytes_popped, RET_kind) { adapter body using `c` }
#define REWRITE(ADDR, NAME, ARGS, POP, RET)                                                          \
    static void adapter_##ADDR(CPU *c);                                                              \
    static Hook hook_##ADDR = {.addr = 0x##ADDR, .name = NAME, .orig = ORIG_##ADDR,                  \
                               .repl = adapter_##ADDR, .args = ARGS, .pop = POP, .ret = RET};        \
    extern "C" void F_##ADDR(CPU *c) { chk_dispatch(&hook_##ADDR, c); }                              \
    [[maybe_unused]] static const bool registered_##ADDR = (chk_register(&hook_##ADDR), true);       \
    static void adapter_##ADDR([[maybe_unused]] CPU *c)

// ------------------------------------------------------------------ calls (rewrite -> original)
namespace guest {

struct Args {
    uint32_t v[16];
    int n = 0;
    void put(uint32_t x) { v[n++] = x; }
    void add(int32_t x) { put(static_cast<uint32_t>(x)); }
    void add(uint32_t x) { put(x); }
    void add(bool x) { put(x); }
    void add(float f) { uint32_t x; std::memcpy(&x, &f, 4); put(x); }
    void add(double d) { uint64_t x; std::memcpy(&x, &d, 8); put(static_cast<uint32_t>(x)); put(static_cast<uint32_t>(x >> 32)); }
    template <class T> void add(T *p) { put(addr(p)); }
    template <class T> void add(ptr32<T> p) { put(p.a); }
};

template <class... A> inline uint32_t call_regs(uint32_t fn, uint32_t ecx, uint32_t edx, A... a) {
    CPU *c = cpu();
    Args args;
    (args.add(a), ...);
    ECX = ecx;
    EDX = edx;
    return guest_call(c, fn, args.n, args.v);
}
template <class... A> inline uint32_t call_cdecl(uint32_t fn, A... a) { return call_regs(fn, 0, 0, a...); }
// an imported function (SDL, CRT, Windows), through its import table slot (bridge/imports.h)
template <class... A> inline uint32_t call_import(uint32_t slot, A... a) { return call_regs(at<uint32_t>(slot), 0, 0, a...); }
template <class... A> inline uint32_t call_stdcall(uint32_t fn, A... a) { return call_regs(fn, 0, 0, a...); }
template <class S, class... A> inline uint32_t call_thiscall(uint32_t fn, S *self, A... a) {
    return call_regs(fn, addr(self), 0, a...);
}
inline void *op_new(uint32_t n) { return host(call_cdecl(0x496bdc, n)); }
inline void op_delete(void *p, uint32_t n) { call_cdecl(0x496c1d, p, n); }

// result of a function returning float/double in x87 ST0 (popped)
inline double st0() { CPU *c = cpu(); double v = ST0_D; fpu_pop(c); return v; }
inline float xmm0() { CPU *c = cpu(); return XMM_F(0, 0); }
inline void set_xmm(int n, float v) { CPU *c = cpu(); XMM_F(n, 0) = v; }

// Scratch memory on the guest stack, for objects whose address is handed to original code.
template <class T> struct Local {
    uint32_t saved, a;
    Local() {
        CPU *c = cpu();
        saved = ESP;
        ESP = (ESP - static_cast<uint32_t>(sizeof(T))) & ~15u;
        a = ESP;
        std::memset(host(a), 0, sizeof(T));
    }
    ~Local() { CPU *c = cpu(); ESP = saved; }
    Local(const Local &) = delete;
    T *get() const { return &at<T>(a); }
    T *operator->() const { return get(); }
    T &operator*() const { return *get(); }
};

// A function-local static's thread-safe first use, as MSVC compiles it: when the guard (a guest int) is
// newer than this thread's epoch (TLS block + 4), _Init_thread_header (0x49734e) claims it; a guard
// of -1 then means this thread initialises: init(), then _Init_thread_footer (0x49730f).
template <class F> void static_init(uint32_t guard, F init) {
    CPU *c = cpu();
    const uint32_t tls = at<uint32_t>(at<uint32_t>(FS_BASE + 0x2c));
    if (at<int32_t>(guard) <= at<int32_t>(tls + 4)) return;
    call_cdecl(0x49734e, guard);
    if (at<int32_t>(guard) != -1) return;
    init();
    call_cdecl(0x49730f, guard);
}

}  // namespace guest
