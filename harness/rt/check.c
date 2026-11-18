// Comparison harness (see check.h).
#include "check.h"
#include "rt.h"
#include "plat.h"
#include "image.h"
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

_Thread_local CPU *chk_cpu;
int chk_in_shadow_child;
uint32_t chk_frame;

static Hook *hooks;
static int shadow_max = 50;

void chk_register(Hook *h) { h->next = hooks; hooks = h; }

static uint64_t hash_bytes(const uint8_t *p, size_t n, uint64_t seed) {
    const uint64_t K1 = 0x9e3779b97f4a7c15ull, K2 = 0xc2b2ae3d27d4eb4full;
    uint64_t a = seed ^ K1, b = seed ^ K2;
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        uint64_t x, y;
        memcpy(&x, p + i, 8); memcpy(&y, p + i + 8, 8);
        a = (a ^ x) * K1; a ^= a >> 29;
        b = (b ^ y) * K2; b ^= b >> 31;
    }
    for (; i < n; i++) a = (a ^ p[i]) * K1;
    return a ^ (b * 0xff51afd7ed558ccdull) ^ n;
}

// ------------------------------------------------------------------ imports a shadow child may call
// Only imports whose effects stay inside guest memory (or host state private to the child). Anything else
// (files, window, events, audio device, process exit) ends the check: see chk_extern.
static const char *const pure_imports[] = {
    "memchr", "memcpy", "memmove", "memset", "strchr", "strrchr", "_stricmp", "strncmp", "strncpy", "lstrlenA",
    "CompareStringA", "atof", "atoi", "malloc", "free", "_callnewh", "floor", "_libm_sse2_cos_precise",
    "_libm_sse2_sin_precise", "_libm_sse2_sqrt_precise", "_libm_sse2_pow_precise", "_libm_sse2_log10_precise",
    "rand", "srand", "__stdio_common_vsprintf", "__stdio_common_vsscanf", "_errno", "SDL_GetTicks",
    "QueryPerformanceCounter", "GetSystemTime", "GetSystemTimeAsFileTime", "_time64", "GetDateFormatA",
    "EnterCriticalSection", "LeaveCriticalSection", "GetCurrentThreadId", "SDL_FillRect", "SDL_UpperBlit",
    "SDL_LockSurface", "SDL_UnlockSurface", "SDL_GetRGBA", "SDL_SetAlpha", "SDL_CreateRGBSurface",
    "SDL_FreeSurface", "SDL_ConvertSurface", "SDL_DisplayFormat", "SDL_DisplayFormatAlpha",
    "SDL_GetVideoSurface", "SDL_GetKeyState", "SDL_SetPalette", "SDL_Delay", "SDL_BuildAudioCVT",
    "SDL_ConvertAudio", "SDL_FreeWAV", NULL};

int chk_pure_import(const char *name) {
    if (name[0] == '?') return strstr(name, "_Fiopen") == NULL;   // MSVCP140 (mangled): guest-memory streams
    for (const char *const *p = pure_imports; *p; p++)
        if (!strcmp(*p, name)) return 1;
    return 0;
}

// ------------------------------------------------------------------ hook selection
static int in_list(const char *list, const Hook *h) {
    if (!list) return 0;
    if (!strcmp(list, "all")) return 1;
    const char *p = list;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char tok[96];
        if (n >= sizeof tok) n = sizeof tok - 1;
        memcpy(tok, p, n); tok[n] = 0;
        if (strtoul(tok, NULL, 16) == h->addr || !strcmp(tok, h->name)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static FILE *trace_f;
static int trace_heap_every = 30;

void chk_init(void) {
    const char *o = getenv("GRX_ORIG"), *s = getenv("GRX_SHADOW"), *n = getenv("GRX_SHADOW_N");
    if (n) shadow_max = atoi(n);
    int nh = 0, no = 0, ns = 0;
    for (Hook *h = hooks; h; h = h->next) {
        nh++;
        h->use_orig = (uint8_t)in_list(o, h);
        h->shadow = (uint8_t)(!h->use_orig && in_list(s, h));
        no += h->use_orig;
        ns += h->shadow;
    }
    rt_log("rewrite: %d functions replaced, %d forwarded to the original, %d shadow-checked", nh, no, ns);
#ifdef _WIN32
    if (ns) rt_log("shadow checks need fork(): not available on Windows");
#endif
    const char *t = getenv("GRX_TRACE");
    if (t) trace_f = fopen(t, "w");
    const char *th = getenv("GRX_TRACE_HEAP");
    if (th) trace_heap_every = atoi(th);
}

void chk_exit(int code) {
    int bad = 0;
    for (Hook *h = hooks; h; h = h->next) {
        if (h->checks || h->mismatches || h->unshadowable)
            rt_log("shadow %08x %-40s calls %u checked %u mismatches %u%s", h->addr, h->name, h->calls, h->checks,
                   h->mismatches, h->unshadowable ? " (not checkable: touches files/window)" : "");
        bad += h->mismatches != 0;
    }
    if (trace_f) { fprintf(trace_f, "END frames=%u shadow_bad=%d\n", chk_frame, bad); fclose(trace_f); trace_f = NULL; }
    fflush(NULL);
    exit(code);
}

// ------------------------------------------------------------------ whole-run trace
static uint64_t audio_h;
void chk_audio_hash(const uint8_t *buf, uint32_t len) { audio_h = hash_bytes(buf, len, audio_h); }

uint32_t rt_heap_top(void);
static uint64_t hash_range(uint32_t lo, uint32_t hi, uint64_t h) { return hash_bytes(GP(lo), hi - lo, h); }

void chk_frame_end(const uint8_t *pixels, int w, int h, int pitch, const uint8_t *pal) {
    chk_frame++;
    if (!trace_f) return;
    uint64_t sh = hash_bytes(pal, 1024, 0);
    for (int y = 0; y < h; y++) sh = hash_bytes(pixels + (size_t)y * pitch, (size_t)w, sh);
    uint64_t dh = hash_range(img_base, img_base + img_size, 0);
    fprintf(trace_f, "F %u t=%llu scr=%016llx img=%016llx aud=%016llx", chk_frame,
            (unsigned long long)(plat_ticks_us() / 1000), (unsigned long long)sh, (unsigned long long)dh,
            (unsigned long long)audio_h);
    if (trace_heap_every > 0 && chk_frame % (uint32_t)trace_heap_every == 0)
        fprintf(trace_f, " heap=%016llx", (unsigned long long)hash_range(HEAP_BASE, rt_heap_top(), 0));
    fputc('\n', trace_f);
}

// The adapter only reads arguments and sets result registers; returning (pop the return address and, for
// callee-cleanup conventions, the arguments) is done here, the same for every function.
static void run_repl(Hook *h, CPU *c) {
    uint32_t esp0 = ESP;
    h->repl(c);
    if (ESP != esp0) rt_fatal(c, "rewrite of %08x %s left esp %08x (entry %08x)", h->addr, h->name, ESP, esp0);
    c->ret = G32(esp0);
    ESP = esp0 + 4 + h->pop;
}

// ------------------------------------------------------------------ shadow execution
typedef struct {
    uint32_t eax, edx, esp, ret, ebx, esi, edi, ebp;
    double st0;
    uint64_t xmm0;
} Regs;

static void get_regs(CPU *c, Regs *r) {
    r->eax = EAX; r->edx = EDX; r->esp = ESP; r->ret = c->ret;
    r->ebx = EBX; r->esi = ESI; r->edi = EDI; r->ebp = EBP;
    r->st0 = ST0_D;
    memcpy(&r->xmm0, c->r + 0x1200, 8);
}

typedef struct { uint32_t lo, hi; } Range;
uint32_t rt_stack_top(void);
// guest memory that must be identical after the call. On the calling thread's stack only the caller's
// frames count: below the entry ESP is the callee's scratch, and its stack arguments are dead after return.
static int shadow_ranges(CPU *c, uint32_t esp0, int args, Range *r) {
    int n = 0;
    r[n++] = (Range){img_base, img_base + ((img_size + 0xfff) & ~0xfffu)};
    r[n++] = (Range){TEB_AREA, THUNK_BASE};
    uint32_t keep = esp0 + 4 + 4u * (uint32_t)args;
    if (STACK_AREA < c->stack_lo) r[n++] = (Range){STACK_AREA, c->stack_lo};
    r[n++] = (Range){esp0, esp0 + 4};
    if (keep < c->stack_hi) r[n++] = (Range){keep, c->stack_hi};
    if (c->stack_hi < rt_stack_top()) r[n++] = (Range){c->stack_hi, rt_stack_top()};
    r[n++] = (Range){HEAP_BASE, (rt_heap_top() + 0xfff) & ~0xfffu};
    return n;
}

typedef struct { uint32_t addr, len; uint64_t h; } PageH;

static PageH *page_hashes(Range *r, int nr, uint32_t *count) {
    uint32_t total = 0;
    for (int i = 0; i < nr; i++) total += (r[i].hi - r[i].lo + 0xfff) / 0x1000;
    PageH *ph = malloc(sizeof(PageH) * (total + 1));
    uint32_t k = 0;
    for (int i = 0; i < nr; i++)
        for (uint32_t a = r[i].lo; a < r[i].hi; a += 0x1000) {
            uint32_t len = r[i].hi - a < 0x1000 ? r[i].hi - a : 0x1000;
            ph[k++] = (PageH){a, len, hash_bytes(GP(a), len, a)};
        }
    *count = k;
    return ph;
}

#ifndef _WIN32
static int wr_all(int fd, const void *p, size_t n) {
    const uint8_t *b = p;
    while (n) { ssize_t k = write(fd, b, n); if (k <= 0) return -1; b += k; n -= (size_t)k; }
    return 0;
}
static int rd_all(int fd, void *p, size_t n) {
    uint8_t *b = p;
    while (n) { ssize_t k = read(fd, b, n); if (k <= 0) return -1; b += k; n -= (size_t)k; }
    return 0;
}

static int shadow_out = -1, shadow_in = -1;
void chk_extern(const char *what) {
    if (!chk_in_shadow_child) return;
    uint32_t hdr[2] = {0x4c494142u /* BAIL */, 0};
    char name[64] = {0};
    snprintf(name, sizeof name, "%s", what);
    wr_all(shadow_out, hdr, sizeof hdr);
    wr_all(shadow_out, name, sizeof name);
    _exit(0);
}

static const char *region_name(uint32_t a) {
    if (a >= img_base && a < img_base + img_size) return "image";
    if (a >= HEAP_BASE) return "heap";
    if (a >= STACK_AREA) return "stack";
    return "runtime data";
}

static void shadow_run(Hook *h, CPU *c) {
    uint32_t esp0 = ESP;
    int to_child[2], to_parent[2];
    if (pipe(to_child) || pipe(to_parent)) { run_repl(h, c); return; }
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { run_repl(h, c); return; }
    if (pid == 0) {
        chk_in_shadow_child = 1;
        shadow_out = to_parent[1];
        shadow_in = to_child[0];
        h->orig(c);
        Regs r;
        get_regs(c, &r);
        Range rg[8];
        int nr = shadow_ranges(c, esp0, h->args, rg);
        uint32_t n;
        PageH *ph = page_hashes(rg, nr, &n);
        uint32_t hdr[2] = {0x454e4f44u /* DONE */, n};
        wr_all(shadow_out, hdr, sizeof hdr);
        wr_all(shadow_out, &r, sizeof r);
        wr_all(shadow_out, ph, sizeof(PageH) * n);
        for (;;) {   // page contents on request
            uint32_t a;
            if (rd_all(shadow_in, &a, 4) || a == 0) break;
            wr_all(shadow_out, GP(a), 0x1000);
        }
        _exit(0);
    }
    close(to_child[0]);
    close(to_parent[1]);
    run_repl(h, c);
    Regs mine;
    get_regs(c, &mine);
    uint32_t hdr[2];
    int fd = to_parent[0];
    if (rd_all(fd, hdr, sizeof hdr)) {
        rt_log("shadow %08x %s: original crashed or exited in the shadow child (see log)", h->addr, h->name);
        h->mismatches++;
    } else if (hdr[0] == 0x4c494142u) {
        char name[64];
        rd_all(fd, name, sizeof name);
        rt_log("shadow %08x %s: not checkable, the original calls %s", h->addr, h->name, name);
        h->unshadowable = 1;
        h->shadow = 0;
    } else {
        Regs theirs;
        rd_all(fd, &theirs, sizeof theirs);
        PageH *tp = malloc(sizeof(PageH) * (hdr[1] + 1));
        rd_all(fd, tp, sizeof(PageH) * hdr[1]);
        Range rg[8];
        int nr = shadow_ranges(c, esp0, h->args, rg);
        uint32_t n;
        PageH *mp = page_hashes(rg, nr, &n);
        char why[512];
        int wl = 0, bad = 0;
#define WHY(...) (wl += snprintf(why + wl, sizeof why - (size_t)wl > 0 ? sizeof why - (size_t)wl : 0, __VA_ARGS__))
        if (mine.esp != theirs.esp) { bad = 1; WHY(" esp %08x/%08x", theirs.esp, mine.esp); }
        if (mine.ret != theirs.ret) { bad = 1; WHY(" return address %08x/%08x", theirs.ret, mine.ret); }
        if (mine.ebx != theirs.ebx || mine.esi != theirs.esi || mine.edi != theirs.edi || mine.ebp != theirs.ebp) {
            bad = 1;
            WHY(" callee-saved ebx %08x/%08x esi %08x/%08x edi %08x/%08x ebp %08x/%08x", theirs.ebx, mine.ebx,
                theirs.esi, mine.esi, theirs.edi, mine.edi, theirs.ebp, mine.ebp);
        }
        if ((h->ret == RET_EAX || h->ret == RET_EDXEAX) && mine.eax != theirs.eax) { bad = 1; WHY(" eax %08x/%08x", theirs.eax, mine.eax); }
        if (h->ret == RET_EDXEAX && mine.edx != theirs.edx) { bad = 1; WHY(" edx %08x/%08x", theirs.edx, mine.edx); }
        if (h->ret == RET_ST0 && memcmp(&mine.st0, &theirs.st0, 8)) { bad = 1; WHY(" st0 %.9g/%.9g", theirs.st0, mine.st0); }
        if (h->ret == RET_XMM0 && mine.xmm0 != theirs.xmm0) { bad = 1; WHY(" xmm0 %016llx/%016llx", (unsigned long long)theirs.xmm0, (unsigned long long)mine.xmm0); }
        // pages: both lists are sorted by address within the same ranges (the heap end may differ)
        uint32_t diff[8], nd = 0, extra = 0;
        uint32_t i = 0, j = 0;
        while (i < hdr[1] || j < n) {
            if (j >= n || (i < hdr[1] && tp[i].addr < mp[j].addr)) { extra++; i++; continue; }
            if (i >= hdr[1] || mp[j].addr < tp[i].addr) { extra++; j++; continue; }
            if (tp[i].h != mp[j].h) { if (nd < 8) diff[nd] = mp[j].addr; nd++; }
            i++; j++;
        }
        if (nd || extra) { bad = 1; WHY(" %u pages differ%s", nd, extra ? ", heap size differs" : ""); }
        h->checks++;
        if (bad) {
            h->mismatches++;
            if (h->mismatches <= 5) {
                rt_log("SHADOW MISMATCH %08x %s (call %u) [original/rewrite]:%s", h->addr, h->name, h->calls, why);
                uint8_t page[0x1000];
                for (uint32_t k = 0; k < nd && k < 8; k++) {
                    wr_all(to_child[1], &diff[k], 4);
                    if (rd_all(fd, page, sizeof page)) break;
                    int shown = 0;
                    for (uint32_t o = 0; o < 0x1000 && shown < 6; o += 4) {
                        uint32_t a, b;
                        memcpy(&a, page + o, 4);
                        memcpy(&b, GP(diff[k] + o), 4);
                        if (a != b) {
                            rt_log("    %s %08x: %08x / %08x", region_name(diff[k] + o), diff[k] + o, a, b);
                            shown++;
                        }
                    }
                }
            }
        }
        free(tp);
        free(mp);
    }
    uint32_t zero = 0;
    wr_all(to_child[1], &zero, 4);
    close(to_child[1]);
    close(fd);
    waitpid(pid, NULL, 0);
}
#else
void chk_extern(const char *what) { (void)what; }
static void shadow_run(Hook *h, CPU *c) { run_repl(h, c); }
#endif

void chk_dispatch(Hook *h, CPU *c) {
    h->calls++;
    if (h->use_orig) { h->orig(c); return; }
    CPU *saved = chk_cpu;
    chk_cpu = c;
    if (h->shadow && !chk_in_shadow_child && h->checks < (uint32_t)shadow_max) shadow_run(h, c);
    else run_repl(h, c);
    chk_cpu = saved;
}
