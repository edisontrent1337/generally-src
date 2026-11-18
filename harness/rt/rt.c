// Core runtime: guest memory arena, image loader, import thunks, dispatch, heap, threads, main.
#include "rt.h"
#include "image.h"
#include "plat.h"
#include "check.h"
#include <stdarg.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

uint8_t *MEM;
int rt_quit_requested;

// ------------------------------------------------------------------ logging / fatal
static FILE *logf_;
static plat_mutex log_mx;

void rt_log(const char *fmt, ...) {
    va_list ap;
    if (chk_in_shadow_child) return;
    plat_mutex_lock(&log_mx);
    if (!logf_) logf_ = fopen("grx.log", "w");
    va_start(ap, fmt);
    if (logf_) { vfprintf(logf_, fmt, ap); fputc('\n', logf_); fflush(logf_); }
    va_end(ap);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
    plat_mutex_unlock(&log_mx);
}

static int in_text(uint32_t a) {
    for (unsigned i = 0; i < img_nsections; i++) {
        const ImgSection *s = &img_sections[i];
        if ((!strcmp(s->name, "text")) && a >= s->va && a < s->va + s->size) return 1;
    }
    return 0;
}

_Noreturn void rt_fatal(CPU *c, const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (chk_in_shadow_child) _exit(1);
    rt_log("FATAL: %s", msg);
    if (c) {
        rt_log("  eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x",
               EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI);
        uint32_t sp = ESP;
        int n = 0;
        rt_log("  stack (code addresses):");
        for (int i = 0; i < 2048 && n < 24; i++) {
            uint32_t v = G32(sp + 4 * i);
            if (in_text(v)) { rt_log("    [esp+%x] %08x", 4 * i, v); n++; }
        }
    }
    plat_message_box("GeneRally port error", msg);
    exit(1);
}

// ------------------------------------------------------------------ dispatch
static ShimFn thunk_fn[THUNK_MAX];
static uint64_t thunk_calls[THUNK_MAX];
static const char *thunk_name[THUNK_MAX];
static uint8_t thunk_pure[THUNK_MAX];
static unsigned nthunks;

static ShimFn find_shim(const char *name) {
    const ShimDef *tabs[] = {shims_k32, shims_crt, shims_msvcp, shims_sdl};
    for (unsigned t = 0; t < sizeof tabs / sizeof *tabs; t++)
        for (const ShimDef *d = tabs[t]; d->name; d++)
            if (!strcmp(d->name, name)) return d->fn;
    return NULL;
}

uint32_t rt_thunk_for(const char *name) {
    for (unsigned i = 0; i < nthunks; i++)
        if (!strcmp(thunk_name[i], name)) return THUNK_BASE + 16 * i;
    ShimFn f = find_shim(name);
    if (!f) return 0;
    if (nthunks >= THUNK_MAX) rt_fatal(NULL, "too many thunks");
    thunk_fn[nthunks] = f;
    thunk_name[nthunks] = name;
    thunk_pure[nthunks] = (uint8_t)chk_pure_import(name);
    return THUNK_BASE + 16 * nthunks++;
}

// per-thread cache: the game, audio and network threads all run guest code
static _Thread_local void (*lookup_fn_cache_v[1024])(CPU *);
static _Thread_local uint32_t lookup_fn_cache_a[1024];

static void (*find_fn(uint32_t a))(CPU *) {
    unsigned h = (a * 2654435761u) >> 22;
    if (lookup_fn_cache_a[h] == a && lookup_fn_cache_v[h]) return lookup_fn_cache_v[h];
    unsigned lo = 0, hi = rt_nfn;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (rt_fntab[mid].addr < a) lo = mid + 1; else hi = mid;
    }
    if (lo < rt_nfn && rt_fntab[lo].addr == a) {
        lookup_fn_cache_a[h] = a;
        lookup_fn_cache_v[h] = rt_fntab[lo].fn;
        return rt_fntab[lo].fn;
    }
    return NULL;
}

void rt_call(CPU *c, uint32_t t) {
    if (t - THUNK_BASE < 16u * THUNK_MAX) {
        unsigned i = (t - THUNK_BASE) / 16;
        if (i < nthunks && thunk_fn[i]) {
            if (chk_in_shadow_child && !thunk_pure[i]) chk_extern(thunk_name[i]);
            thunk_calls[i]++;
            thunk_fn[i](c);
            return;
        }
        rt_fatal(c, "call to unimplemented import %s", i < nthunks ? thunk_name[i] : "?");
    }
    void (*f)(CPU *) = find_fn(t);
    if (!f) rt_fatal(c, "call to unknown code address %08x", t);
    f(c);
}

void rt_jump(CPU *c, uint32_t t) { rt_call(c, t); }

void rt_badret(CPU *c, uint32_t expect) {
    rt_fatal(c, "unexpected return: to %08x, expected %08x", c->ret, expect);
}

void rt_trap(CPU *c, uint32_t at) {
    rt_fatal(c, "guest trap (int3/fastfail) at %08x", at);
}

void rt_nofall(CPU *c, uint32_t at) {
    rt_fatal(c, "fell through into non-code at %08x (after a no-return call?)", at);
}

void rt_cpuid(CPU *c) {
    uint32_t leaf = EAX;
    switch (leaf) {
    case 0: EAX = 1; EBX = 0x756e6547; EDX = 0x49656e69; ECX = 0x6c65746e; break;  // GenuineIntel
    case 1: EAX = 0x000306a9; EBX = 0; ECX = 0x00000001; EDX = 0x078bfbff; break;  // SSE..SSE3, no AVX
    default: EAX = EBX = ECX = EDX = 0; break;
    }
}

// x87 extended <-> double
uint64_t ld_f80(uint8_t *m, uint32_t a) {
    uint64_t mant = *(u64u *)(m + a);
    uint16_t se = *(u16u *)(m + a + 8);
    int sign = se >> 15, exp = se & 0x7fff;
    double v;
    if (exp == 0 && mant == 0) v = 0.0;
    else if (exp == 0x7fff) v = (mant << 1) ? __builtin_nan("") : __builtin_inf();
    else v = __builtin_ldexp((double)mant, exp - 16383 - 63);
    if (sign) v = -v;
    return b8(v);
}

void st_f80(uint8_t *m, uint32_t a, uint64_t bits) {
    double v = f8(bits);
    uint64_t mant = 0;
    uint16_t se = 0;
    if (v != v) { se = 0x7fff; mant = 0xc000000000000000ull; }
    else {
        if (__builtin_signbit(v)) { se = 0x8000; v = -v; }
        if (v == __builtin_inf()) { se |= 0x7fff; mant = 0x8000000000000000ull; }
        else if (v != 0.0) {
            int e;
            double fr = __builtin_frexp(v, &e);  // v = fr * 2^e, fr in [0.5,1)
            mant = (uint64_t)__builtin_ldexp(fr, 64);
            se |= (uint16_t)(e - 1 + 16383);
        }
    }
    *(u64u *)(m + a) = mant;
    *(u16u *)(m + a + 8) = se;
}

void fpu_pop(CPU *c) {
    for (int i = 0; i < 7; i++) memcpy(c->r + 0x1100 + 0x10 * i, c->r + 0x1100 + 0x10 * (i + 1), 10);
}

void fpu_push(CPU *c, double v) {
    for (int i = 7; i > 0; i--) memcpy(c->r + 0x1100 + 0x10 * i, c->r + 0x1100 + 0x10 * (i - 1), 10);
    ST0_D = v;
}

// ------------------------------------------------------------------ host -> guest calls
uint32_t guest_call(CPU *c, uint32_t fn, int nargs, const uint32_t *args) {
    uint32_t saved_esp = ESP, saved_ret = c->ret;
    uint32_t sp = ESP - 64;  // keep clear of anything the current shim still reads
    for (int i = nargs - 1; i >= 0; i--) { sp -= 4; G32(sp) = args[i]; }
    sp -= 4;
    G32(sp) = MAGIC_RET;
    ESP = sp;
    rt_call(c, fn);
    if (c->ret != MAGIC_RET) rt_fatal(c, "callback %08x returned to %08x", fn, c->ret);
    ESP = saved_esp;
    c->ret = saved_ret;
    return EAX;
}

uint32_t guest_thiscall(CPU *c, uint32_t fn, uint32_t self, int nargs, const uint32_t *args) {
    uint32_t saved_ecx = ECX;
    ECX = self;
    uint32_t r = guest_call(c, fn, nargs, args);
    ECX = saved_ecx;
    return r;
}

// ------------------------------------------------------------------ guest heap
// Power-of-two size classes, 16-byte header holding the class; freed blocks kept on per-class lists.
static plat_mutex heap_mx;
static uint32_t heap_top = HEAP_BASE, heap_committed = HEAP_BASE;
static uint32_t freelist[40];

static uint32_t heap_bump(uint32_t n) {
    if (heap_top + n > HEAP_END || heap_top + n < heap_top) return 0;
    uint32_t p = heap_top;
    heap_top += n;
    if (heap_top > heap_committed) {
        uint32_t want = (heap_top + 0xfffff) & ~0xfffffu;
        plat_commit(MEM + heap_committed, want - heap_committed);
        heap_committed = want;
    }
    return p;
}

uint32_t gmalloc(uint32_t size) {
    unsigned k = 4;  // 16 bytes minimum (including header)
    uint64_t need = (uint64_t)size + 16;
    while ((1ull << k) < need) k++;
    if (k >= 40) return 0;
    plat_mutex_lock(&heap_mx);
    uint32_t blk = freelist[k];
    if (blk) freelist[k] = G32(blk + 16);
    else blk = heap_bump(1u << k);
    plat_mutex_unlock(&heap_mx);
    if (!blk) return 0;
    G32(blk) = k;
    G32(blk + 4) = 0x48454150;  // "HEAP"
    G32(blk + 8) = size;
    return blk + 16;
}

void gfree(uint32_t p) {
    if (!p) return;
    uint32_t blk = p - 16;
    if (G32(blk + 4) != 0x48454150) { rt_log("gfree: bad pointer %08x", p); return; }
    unsigned k = G32(blk);
    G32(blk + 4) = 0x46524545;  // "FREE"
    plat_mutex_lock(&heap_mx);
    G32(blk + 16) = freelist[k];
    freelist[k] = blk;
    plat_mutex_unlock(&heap_mx);
}

uint32_t grealloc(uint32_t p, uint32_t size) {
    if (!p) return gmalloc(size);
    if (!size) { gfree(p); return 0; }
    uint32_t blk = p - 16;
    uint32_t cap = (1u << G32(blk)) - 16;
    if (size <= cap) { G32(blk + 8) = size; return p; }
    uint32_t q = gmalloc(size);
    if (!q) return 0;
    memcpy(GP(q), GP(p), G32(blk + 8));
    gfree(p);
    return q;
}

uint32_t gcalloc(uint32_t n, uint32_t size) {
    uint64_t t = (uint64_t)n * size;
    if (t > 0xffffffffu) return 0;
    uint32_t p = gmalloc((uint32_t)t);
    if (p) memset(GP(p), 0, (size_t)t);
    return p;
}

uint32_t gstrdup(const char *s) {
    uint32_t n = (uint32_t)strlen(s) + 1;
    uint32_t p = gmalloc(n);
    memcpy(GP(p), s, n);
    return p;
}

uint32_t rt_heap_top(void) { return heap_top; }

static uint32_t data_top = DATA_AREA;
uint32_t gdata_alloc(uint32_t size) {
    plat_mutex_lock(&heap_mx);
    uint32_t p = (data_top + 15) & ~15u;
    data_top = p + size;
    plat_mutex_unlock(&heap_mx);
    if (data_top > THUNK_BASE) rt_fatal(NULL, "guest data area full");
    memset(GP(p), 0, size);
    return p;
}

// ------------------------------------------------------------------ threads / TEB
static plat_mutex cpu_mx;
static uint32_t stack_top = STACK_AREA;
static int cpu_count;
static uint32_t peb_addr;

CPU *rt_new_cpu(uint32_t stack_size) {
    CPU *c = plat_aligned_alloc(64, sizeof(CPU));
    memset(c, 0, sizeof *c);
    plat_mutex_lock(&cpu_mx);
    int id = cpu_count++;
    uint32_t lo = stack_top;
    stack_top += stack_size + 0x10000;  // guard gap
    plat_mutex_unlock(&cpu_mx);
    if (id >= 64) rt_fatal(NULL, "too many guest threads");
    plat_commit(MEM + lo, stack_size);
    c->id = id;
    c->stack_lo = lo;
    c->stack_hi = lo + stack_size;
    ESP = c->stack_hi - 256;
    uint32_t teb = TEB_AREA + 0x1000 * id;
    memset(GP(teb), 0, 0x1000);
    G32(teb + 0x00) = 0xffffffffu;   // SEH chain end
    G32(teb + 0x04) = c->stack_hi;   // stack base
    G32(teb + 0x08) = c->stack_lo;   // stack limit
    G32(teb + 0x18) = teb;           // self
    G32(teb + 0x20) = 0x1000;        // process id
    G32(teb + 0x24) = 0x2000 + id;   // thread id
    G32(teb + 0x2c) = teb + 0x800;   // TLS slot array
    G32(teb + 0x30) = peb_addr;
    // static TLS block (.tls template)
    if (img_tls[0]) {
        uint32_t n = img_tls[1] - img_tls[0];
        uint32_t blk = gcalloc(1, n + img_tls[4] + 16);
        memcpy(GP(blk), GP(img_tls[0]), n);
        uint32_t idx = G32(img_tls[2]);
        G32(teb + 0x800 + 4 * idx) = blk;
    }
    FS_BASE = teb;
    R4(0x114) = 0;  // GS
    REG_DF = 0;
    R4(0x1094) = 0x1f80;  // MXCSR default
    return c;
}

void rt_free_cpu(CPU *c) { plat_aligned_free(c); }
uint32_t rt_stack_top(void) { return stack_top; }

// ------------------------------------------------------------------ paths
void host_path(const char *g, char *out, size_t outsz) {
    size_t n = 0;
    // strip drive letter of absolute Windows paths ("C:\...") - only relative paths are meaningful
    if (g[0] && g[1] == ':') g += 2;
    for (; *g && n + 1 < outsz; g++) out[n++] = (*g == '\\') ? PLAT_SEP : *g;
    out[n] = 0;
}

// ------------------------------------------------------------------ startup
static void load_image(void) {
    plat_commit(MEM + img_base, (img_size + 0xffff) & ~0xffffu);
    for (unsigned i = 0; i < img_nsections; i++)
        memcpy(MEM + img_sections[i].va, img_sections[i].data, img_sections[i].size);
    plat_commit(MEM + TEB_AREA, 0x00100000);  // TEB, data, thunk areas
    peb_addr = gdata_alloc(0x400);
    int missing = 0;
    for (unsigned i = 0; i < img_nimports; i++) {
        const ImgImport *im = &img_imports[i];
        uint32_t t = rt_thunk_for(im->name);
        if (!t) {
            // keep a named thunk that fails when called, so we can report which import it was
            if (nthunks >= THUNK_MAX) rt_fatal(NULL, "too many thunks");
            thunk_fn[nthunks] = NULL;
            thunk_name[nthunks] = im->name;
            t = THUNK_BASE + 16 * nthunks++;
            missing++;
            rt_log("note: import without shim: %s!%s", im->dll, im->name);
        }
        G32(im->iat) = t;
    }
    if (missing) rt_log("%d imports have no shim (fatal only if called)", missing);
}

void rt_dump_stats(void) {
    rt_log("import call counts:");
    for (unsigned i = 0; i < nthunks; i++)
        if (thunk_calls[i]) rt_log("  %10llu %s", (unsigned long long)thunk_calls[i], thunk_name[i]);
}

void rt_data_imports(void);   // shim_msvcp.c: points data imports at guest variables
void plat_init(int argc, char **argv);

int main(int argc, char **argv) {
    plat_init(argc, argv);
    plat_mutex_init(&log_mx);
    plat_mutex_init(&heap_mx);
    plat_mutex_init(&cpu_mx);
    MEM = plat_reserve_arena();
    if (!MEM) { fprintf(stderr, "cannot reserve 4 GB guest arena\n"); return 1; }
    load_image();
    rt_data_imports();
    chk_init();
    CPU *c = rt_new_cpu(MAIN_STACK);
    if (img_tls[3]) {
        for (uint32_t p = img_tls[3]; G32(p); p += 4) {
            uint32_t a[3] = {img_base, 1, 0};
            guest_call(c, G32(p), 3, a);
        }
    }
    rt_log("GeneRally port: starting at %08x", img_entry);
    guest_call(c, img_entry, 0, NULL);
    chk_exit((int)EAX);
}
