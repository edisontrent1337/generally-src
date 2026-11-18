// VCRUNTIME140 + UCRT (api-ms-win-crt-*) imports.
#include "rt.h"
#include "plat.h"
#include "check.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define ftello _ftelli64
#define fseeko _fseeki64
#define off_t long long
#else
#include <unistd.h>
#endif

#define SHIM(n) static void n(CPU *c)

// ------------------------------------------------------------------ memory / strings (cdecl)
SHIM(c_memset) { memset(GP(ARG(0)), (int)ARG(1), ARG(2)); ret_c(c, ARG(0)); }
SHIM(c_memcpy) { memmove(GP(ARG(0)), GP(ARG(1)), ARG(2)); ret_c(c, ARG(0)); }
SHIM(c_memmove) { memmove(GP(ARG(0)), GP(ARG(1)), ARG(2)); ret_c(c, ARG(0)); }
SHIM(c_memchr) {
    void *p = memchr(GP(ARG(0)), (int)ARG(1), ARG(2));
    ret_c(c, p ? GADDR(p) : 0);
}
SHIM(c_strchr) { char *p = strchr(GS(ARG(0)), (int)ARG(1)); ret_c(c, p ? GADDR(p) : 0); }
SHIM(c_strrchr) { char *p = strrchr(GS(ARG(0)), (int)ARG(1)); ret_c(c, p ? GADDR(p) : 0); }
SHIM(c_strncpy) { strncpy(GS(ARG(0)), GS(ARG(1)), ARG(2)); ret_c(c, ARG(0)); }
SHIM(c_strncmp) { ret_c(c, (uint32_t)strncmp(GS(ARG(0)), GS(ARG(1)), ARG(2))); }
SHIM(c_stricmp) {
    const unsigned char *a = (const unsigned char *)GS(ARG(0)), *b = (const unsigned char *)GS(ARG(1));
    int r;
    for (;; a++, b++) {
        r = tolower(*a) - tolower(*b);
        if (r || !*a) break;
    }
    ret_c(c, (uint32_t)r);
}
SHIM(c_tolower) { ret_c(c, (uint32_t)tolower((int)ARG(0))); }
SHIM(c_atoi) { ret_c(c, (uint32_t)atoi(GS(ARG(0)))); }
SHIM(c_atof) { fpu_push(c, atof(GS(ARG(0)))); ret_c(c, EAX); }

// ------------------------------------------------------------------ heap
SHIM(c_malloc) { ret_c(c, gmalloc(ARG(0))); }
SHIM(c_free) { gfree(ARG(0)); ret_c(c, 0); }
SHIM(c_set_new_mode) { ret_c(c, 0); }
SHIM(c_callnewh) { ret_c(c, 0); }

// ------------------------------------------------------------------ math
// _libm_sse2_*: argument(s) in xmm0 (xmm1), result in xmm0
SHIM(m_sin) { XMM_D(0) = sin(XMM_D(0)); ret_c(c, EAX); }
SHIM(m_cos) { XMM_D(0) = cos(XMM_D(0)); ret_c(c, EAX); }
SHIM(m_sqrt) { XMM_D(0) = sqrt(XMM_D(0)); ret_c(c, EAX); }
static int trace_math(void) { static int t = -1; if (t < 0) t = getenv("GRX_TRACEMATH") != NULL; return t; }
SHIM(m_log10) {
    double a = XMM_D(0);
    XMM_D(0) = log10(a);
    if (trace_math()) rt_log("log10(%g) = %g", a, XMM_D(0));
    ret_c(c, EAX);
}
SHIM(m_pow) {
    double a = XMM_D(0), b = XMM_D(1);
    XMM_D(0) = pow(a, b);
    if (trace_math()) rt_log("pow(%g, %g) = %g", a, b, XMM_D(0));
    ret_c(c, EAX);
}
SHIM(m_floor) { fpu_push(c, floor(ARGD(0))); ret_c(c, EAX); }
SHIM(m_setusermatherr) { ret_c(c, 0); }
SHIM(m_except1) { ret_c(c, 0); }

// ------------------------------------------------------------------ runtime / startup
static uint32_t atexit_fns[256];
static int natexit;
static int exiting;

static void run_atexit(CPU *c) {
    while (natexit > 0) {
        uint32_t f = atexit_fns[--natexit];
        guest_call(c, f, 0, NULL);
    }
}

void sdl_shutdown(void);
SHIM(r_exit) {
    rt_log("exit(%d)", (int)ARG(0));
    if (!exiting) { exiting = 1; run_atexit(c); }
    sdl_shutdown();
    chk_exit((int)ARG(0));
}
SHIM(r__exit) { sdl_shutdown(); chk_exit((int)ARG(0)); }
SHIM(r_cexit) { if (!exiting) { exiting = 1; run_atexit(c); } ret_c(c, 0); }
SHIM(r_c_exit) { ret_c(c, 0); }
SHIM(r_initterm) {
    uint32_t a = ARG(0), e = ARG(1);
    for (; a < e; a += 4)
        if (G32(a)) guest_call(c, G32(a), 0, NULL);
    ret_c(c, 0);
}
SHIM(r_initterm_e) {
    uint32_t a = ARG(0), e = ARG(1), r = 0;
    for (; a < e && !r; a += 4)
        if (G32(a)) r = guest_call(c, G32(a), 0, NULL);
    ret_c(c, r);
}
SHIM(r_crt_atexit) {
    if (natexit < 256) atexit_fns[natexit++] = ARG(0);
    ret_c(c, 0);
}
SHIM(r_register_onexit_function) {
    if (natexit < 256) atexit_fns[natexit++] = ARG(1);
    ret_c(c, 0);
}
SHIM(r_initialize_onexit_table) { memset(GP(ARG(0)), 0, 12); ret_c(c, 0); }
static uint32_t empty_str;
SHIM(r_get_narrow_winmain_command_line) { if (!empty_str) empty_str = gstrdup(""); ret_c(c, empty_str); }
SHIM(r_zero) { ret_c(c, 0); }
SHIM(r_controlfp_s) { if (ARG(0)) G32(ARG(0)) = 0x9001f; ret_c(c, 0); }
SHIM(r_terminate) { rt_fatal(c, "terminate() called"); }
SHIM(r_errno) { ret_c(c, FS_BASE + 0x700); }
SHIM(r_invalid_parameter_noinfo) { rt_log("CRT: invalid parameter"); ret_c(c, 0); }
SHIM(r_invalid_parameter_noinfo_noreturn) { rt_fatal(c, "CRT: invalid parameter (noreturn)"); }
SHIM(r_seh_filter_exe) { ret_c(c, 0); }
static uint32_t commode;
SHIM(r_p_commode) { if (!commode) commode = gdata_alloc(4); ret_c(c, commode); }

// vcruntime
SHIM(v_nop_handler) { rt_fatal(c, "structured exception handler invoked"); }
SHIM(v_CxxThrowException) {
    uint32_t obj = ARG(0);
    rt_fatal(c, "C++ exception thrown (object %08x, what=%s)", obj,
             (obj && G32(obj + 4)) ? GS(G32(obj + 4)) : "?");
}
SHIM(v_std_terminate) { rt_fatal(c, "std::terminate"); }
// std::exception data: { const char *what; bool dofree }
SHIM(v_std_exception_copy) {
    uint32_t from = ARG(0), to = ARG(1);
    uint32_t w = G32(from);
    if (G8(from + 4) && w) { G32(to) = gstrdup(GS(w)); G8(to + 4) = 1; }
    else { G32(to) = w; G8(to + 4) = 0; }
    ret_c(c, 0);
}
SHIM(v_std_exception_destroy) {
    uint32_t p = ARG(0);
    if (G8(p + 4)) gfree(G32(p));
    G32(p) = 0; G8(p + 4) = 0;
    ret_c(c, 0);
}
SHIM(v_vcrt_InitializeCriticalSectionEx) {
    // cdecl wrapper around the kernel32 one: reuse it through a thunk call
    uint32_t a[3] = {ARG(0), ARG(1), ARG(2)};
    guest_call(c, rt_thunk_for("InitializeCriticalSectionEx"), 3, a);
    ret_c(c, 1);
}

// ------------------------------------------------------------------ time / rand
SHIM(t_time64) {
    int64_t t; long ns;
    plat_wall_time(&t, &ns);
    if (ARG(0)) G64(ARG(0)) = (uint64_t)t;
    ret_c64(c, (uint64_t)t);
}
static uint32_t rand_state = 1;
SHIM(u_srand) { rand_state = ARG(0); ret_c(c, 0); }
SHIM(u_rand) {
    rand_state = rand_state * 214013u + 2531011u;
    ret_c(c, (rand_state >> 16) & 0x7fff);
}

// ------------------------------------------------------------------ filesystem
SHIM(f_mkdir) {
    char p[1024];
    host_path(GS(ARG(0)), p, sizeof p);
#ifdef _WIN32
    int r = _mkdir(p);
#else
    int r = mkdir(p, 0777);
#endif
    if (r) G32(FS_BASE + 0x700) = errno == EEXIST ? 17 : 2;
    ret_c(c, (uint32_t)r);
}
SHIM(f_unlink) { char p[1024]; host_path(GS(ARG(0)), p, sizeof p); ret_c(c, (uint32_t)remove(p)); }
SHIM(f_chdir) {
    char p[1024];
    host_path(GS(ARG(0)), p, sizeof p);
#ifdef _WIN32
    ret_c(c, (uint32_t)_chdir(p));
#else
    ret_c(c, (uint32_t)chdir(p));
#endif
}
SHIM(f_lock_file) { ret_c(c, 0); }

// ------------------------------------------------------------------ stdio (FILE objects live in guest memory)
// guest FILE: +0 _ptr, +4 _base, +8 _cnt (kept zero so the iostream layer always goes through fgetc/fputc),
//             +0xc host index
#define MAXF 64
static FILE *hfiles[MAXF];
static plat_mutex fmx;
static int fmx_init;

static FILE *hf(uint32_t g) {
    if (!g) return NULL;
    uint32_t i = G32(g + 0xc);
    return i < MAXF ? hfiles[i] : NULL;
}

uint32_t crt_fopen(const char *gpath, const char *mode) {
    char p[1024];
    host_path(gpath, p, sizeof p);
    FILE *f = fopen(p, mode);
    if (!f) return 0;
    if (!fmx_init) { plat_mutex_init(&fmx); fmx_init = 1; }
    plat_mutex_lock(&fmx);
    int i;
    for (i = 1; i < MAXF && hfiles[i]; i++) {}
    if (i == MAXF) { plat_mutex_unlock(&fmx); fclose(f); rt_log("too many open files"); return 0; }
    hfiles[i] = f;
    plat_mutex_unlock(&fmx);
    uint32_t g = gcalloc(1, 32);
    G32(g + 0xc) = (uint32_t)i;
    return g;
}

SHIM(s_fclose) {
    uint32_t g = ARG(0);
    FILE *f = hf(g);
    int r = f ? fclose(f) : -1;
    if (f) { hfiles[G32(g + 0xc)] = NULL; gfree(g); }
    ret_c(c, (uint32_t)r);
}
SHIM(s_fflush) { FILE *f = hf(ARG(0)); ret_c(c, (uint32_t)(f ? fflush(f) : 0)); }
SHIM(s_fgetc) { FILE *f = hf(ARG(0)); ret_c(c, (uint32_t)(f ? fgetc(f) : -1)); }
SHIM(s_fputc) { FILE *f = hf(ARG(1)); ret_c(c, (uint32_t)(f ? fputc((int)ARG(0), f) : -1)); }
SHIM(s_ungetc) { FILE *f = hf(ARG(1)); ret_c(c, (uint32_t)(f ? ungetc((int)ARG(0), f) : -1)); }
SHIM(s_fwrite) {
    FILE *f = hf(ARG(3));
    ret_c(c, f ? (uint32_t)fwrite(GP(ARG(0)), ARG(1), ARG(2), f) : 0);
}
SHIM(s_fgetpos) {
    FILE *f = hf(ARG(0));
    if (!f) { ret_c(c, (uint32_t)-1); return; }
    G64(ARG(1)) = (uint64_t)(int64_t)ftello(f);
    ret_c(c, 0);
}
SHIM(s_fsetpos) {
    FILE *f = hf(ARG(0));
    ret_c(c, (uint32_t)(f ? fseeko(f, (off_t)(int64_t)G64(ARG(1)), SEEK_SET) : -1));
}
SHIM(s_fseeki64) {
    FILE *f = hf(ARG(0));
    int64_t off = (int64_t)((uint64_t)ARG(1) | ((uint64_t)ARG(2) << 32));
    ret_c(c, (uint32_t)(f ? fseeko(f, (off_t)off, (int)ARG(3)) : -1));
}
SHIM(s_setvbuf) { ret_c(c, 0); }
SHIM(s_set_fmode) { ret_c(c, 0); }
SHIM(s_get_stream_buffer_pointers) {
    uint32_t g = ARG(0);
    if (ARG(1)) G32(ARG(1)) = g + 4;   // &_base
    if (ARG(2)) G32(ARG(2)) = g + 0;   // &_ptr
    if (ARG(3)) G32(ARG(3)) = g + 8;   // &_cnt
    ret_c(c, 0);
}

// ---- printf with guest varargs
typedef struct { char *out; size_t cap, len; } Sink;
static void sink_put(Sink *s, const char *p, size_t n) {
    for (size_t i = 0; i < n; i++, s->len++)
        if (s->out && s->len + 1 < s->cap) s->out[s->len] = p[i];
}

int g_vformat(char *out, size_t cap, const char *fmt, uint32_t va) {
    Sink s = {out, cap, 0};
    while (*fmt) {
        if (*fmt != '%') { sink_put(&s, fmt++, 1); continue; }
        const char *start = fmt++;
        if (*fmt == '%') { sink_put(&s, "%", 1); fmt++; continue; }
        char spec[64];
        size_t sl = 0;
        spec[sl++] = '%';
        while (strchr("-+ #0", *fmt) && sl < 40) spec[sl++] = *fmt++;
        if (*fmt == '*') { sl += (size_t)snprintf(spec + sl, sizeof spec - sl, "%d", (int)G32(va)); va += 4; fmt++; }
        else while (isdigit((unsigned char)*fmt) && sl < 50) spec[sl++] = *fmt++;
        if (*fmt == '.') {
            spec[sl++] = *fmt++;
            if (*fmt == '*') { sl += (size_t)snprintf(spec + sl, sizeof spec - sl, "%d", (int)G32(va)); va += 4; fmt++; }
            else while (isdigit((unsigned char)*fmt) && sl < 58) spec[sl++] = *fmt++;
        }
        int is64 = 0;
        for (;;) {
            if (*fmt == 'l' && fmt[1] == 'l') { is64 = 1; fmt += 2; }
            else if (*fmt == 'I' && fmt[1] == '6' && fmt[2] == '4') { is64 = 1; fmt += 3; }
            else if (*fmt == 'I' && fmt[1] == '3' && fmt[2] == '2') { fmt += 3; }
            else if (strchr("hlLzjtw", *fmt) && *fmt) fmt++;
            else break;
        }
        char conv = *fmt ? *fmt++ : 0;
        char buf[512];
        int n = 0;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
            if (is64) {
                spec[sl++] = 'l'; spec[sl++] = 'l'; spec[sl++] = conv; spec[sl] = 0;
                uint64_t v = G64(va); va += 8;
                n = (conv == 'd' || conv == 'i') ? snprintf(buf, sizeof buf, spec, (long long)v) : snprintf(buf, sizeof buf, spec, (unsigned long long)v);
            } else {
                spec[sl++] = conv; spec[sl] = 0;
                uint32_t v = G32(va); va += 4;
                n = (conv == 'd' || conv == 'i') ? snprintf(buf, sizeof buf, spec, (int)v) : snprintf(buf, sizeof buf, spec, v);
            }
            break;
        case 'c':
            spec[sl++] = 'c'; spec[sl] = 0;
            n = snprintf(buf, sizeof buf, spec, (int)G32(va)); va += 4;
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            spec[sl++] = conv; spec[sl] = 0;
            n = snprintf(buf, sizeof buf, spec, f8(G64(va))); va += 8;
            break;
        case 'p':
            n = snprintf(buf, sizeof buf, "%08X", G32(va)); va += 4;
            break;
        case 's': {
            spec[sl++] = 's'; spec[sl] = 0;
            uint32_t p = G32(va); va += 4;
            const char *str = p ? GS(p) : "(null)";
            int need = snprintf(NULL, 0, spec, str);
            if (need >= (int)sizeof buf) {
                char *tmp = malloc((size_t)need + 1);
                snprintf(tmp, (size_t)need + 1, spec, str);
                sink_put(&s, tmp, (size_t)need);
                free(tmp);
                continue;
            }
            n = snprintf(buf, sizeof buf, spec, str);
            break;
        }
        case 'n':
            G32(G32(va)) = (uint32_t)s.len; va += 4;
            continue;
        default:
            sink_put(&s, start, (size_t)(fmt - start));
            continue;
        }
        if (n > 0) sink_put(&s, buf, (size_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
    }
    if (s.out && s.cap) s.out[s.len < s.cap ? s.len : s.cap - 1] = 0;
    return (int)s.len;
}

// __stdio_common_vsprintf(uint64 options, char *buf, size_t n, const char *fmt, locale, va_list)
SHIM(s_common_vsprintf) {
    uint64_t opt = (uint64_t)ARG(0) | ((uint64_t)ARG(1) << 32);
    uint32_t buf = ARG(2), n = ARG(3), fmt = ARG(4), va = ARG(6);
    int len = g_vformat(buf ? GS(buf) : NULL, buf ? n : 0, GS(fmt), va);
    int r = len;
    if (buf && (uint32_t)len >= n) {
        // legacy sprintf/_snprintf: -1 on truncation unless the standard-snprintf option bit (2) is set
        if (!(opt & 2)) r = -1;
    }
    ret_c(c, (uint32_t)r);
}

// __stdio_common_vsscanf(uint64 options, const char *buf, size_t n, const char *fmt, locale, va_list)
SHIM(s_common_vsscanf) {
    const char *in = GS(ARG(2));
    const char *fmt = GS(ARG(4));
    uint32_t va = ARG(6);
    const char *s = in;
    int assigned = 0, any = 0;
    while (*fmt) {
        if (isspace((unsigned char)*fmt)) { while (isspace((unsigned char)*s)) s++; fmt++; continue; }
        if (*fmt != '%') { if (*s != *fmt) break; s++; fmt++; continue; }
        fmt++;
        if (*fmt == '%') { if (*s != '%') break; s++; fmt++; continue; }
        int skip = 0, width = 0, lmod = 0;
        if (*fmt == '*') { skip = 1; fmt++; }
        while (isdigit((unsigned char)*fmt)) width = width * 10 + (*fmt++ - '0');
        for (;;) {
            if (*fmt == 'h') { lmod = lmod == -1 ? -2 : -1; fmt++; }
            else if (*fmt == 'l') { lmod++; fmt++; }
            else if (*fmt == 'L' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') { lmod = 1; fmt++; }
            else if (*fmt == 'I' && fmt[1] == '6' && fmt[2] == '4') { lmod = 2; fmt += 3; }
            else break;
        }
        char conv = *fmt++;
        if (conv != 'c' && conv != '[' && conv != 'n') while (isspace((unsigned char)*s)) s++;
        if (conv == 'n') { if (!skip) { G32(G32(va)) = (uint32_t)(s - in); va += 4; } continue; }
        if (!*s) { if (!any) assigned = -1; break; }
        char tmp[512];
        size_t lim = width ? (size_t)width : sizeof tmp - 1;
        if (lim > sizeof tmp - 1) lim = sizeof tmp - 1;
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
            size_t k = 0;
            while (s[k] && k < lim && !isspace((unsigned char)s[k])) { tmp[k] = s[k]; k++; }
            tmp[k] = 0;
            char *end;
            int base = conv == 'x' || conv == 'X' ? 16 : conv == 'o' ? 8 : conv == 'i' ? 0 : 10;
            long long v = (conv == 'd' || conv == 'i') ? strtoll(tmp, &end, base) : (long long)strtoull(tmp, &end, base);
            if (end == tmp) break;
            s += end - tmp;
            any = 1;
            if (!skip) {
                uint32_t p = G32(va); va += 4;
                if (lmod >= 2) G64(p) = (uint64_t)v;
                else if (lmod == -1) G16(p) = (uint16_t)v;
                else if (lmod == -2) G8(p) = (uint8_t)v;
                else G32(p) = (uint32_t)v;
                assigned++;
            }
        } else if (strchr("feEgGaA", conv)) {
            size_t k = 0;
            while (s[k] && k < lim && !isspace((unsigned char)s[k])) { tmp[k] = s[k]; k++; }
            tmp[k] = 0;
            char *end;
            double v = strtod(tmp, &end);
            if (end == tmp) break;
            s += end - tmp;
            any = 1;
            if (!skip) {
                uint32_t p = G32(va); va += 4;
                if (lmod >= 1) G64(p) = b8(v); else G32(p) = b4((float)v);
                assigned++;
            }
        } else if (conv == 's') {
            uint32_t p = skip ? 0 : G32(va);
            if (!skip) va += 4;
            size_t k = 0;
            while (s[k] && k < lim && !isspace((unsigned char)s[k])) { if (p) G8(p + k) = (uint8_t)s[k]; k++; }
            if (p) G8(p + k) = 0;
            s += k;
            any = 1;
            if (!skip) assigned++;
        } else if (conv == 'c') {
            uint32_t p = skip ? 0 : G32(va);
            if (!skip) va += 4;
            size_t n = width ? (size_t)width : 1, k = 0;
            for (; k < n && s[k]; k++) if (p) G8(p + k) = (uint8_t)s[k];
            if (k < n) break;
            s += k;
            any = 1;
            if (!skip) assigned++;
        } else if (conv == '[') {
            int neg = 0;
            char set[256] = {0};
            if (*fmt == '^') { neg = 1; fmt++; }
            if (*fmt == ']') { set[(unsigned char)']'] = 1; fmt++; }
            while (*fmt && *fmt != ']') {
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    for (int ch = (unsigned char)fmt[0]; ch <= (unsigned char)fmt[2]; ch++) set[ch] = 1;
                    fmt += 3;
                } else set[(unsigned char)*fmt++] = 1;
            }
            if (*fmt == ']') fmt++;
            uint32_t p = skip ? 0 : G32(va);
            if (!skip) va += 4;
            size_t k = 0;
            while (s[k] && k < lim && (set[(unsigned char)s[k]] ^ neg)) { if (p) G8(p + k) = (uint8_t)s[k]; k++; }
            if (!k) break;
            if (p) G8(p + k) = 0;
            s += k;
            any = 1;
            if (!skip) assigned++;
        } else break;
    }
    ret_c(c, (uint32_t)assigned);
}

const ShimDef shims_crt[] = {
    {"memset", c_memset}, {"memcpy", c_memcpy}, {"memmove", c_memmove}, {"memchr", c_memchr},
    {"strchr", c_strchr}, {"strrchr", c_strrchr}, {"strncpy", c_strncpy}, {"strncmp", c_strncmp},
    {"_stricmp", c_stricmp}, {"tolower", c_tolower}, {"atoi", c_atoi}, {"atof", c_atof},
    {"malloc", c_malloc}, {"free", c_free}, {"_set_new_mode", c_set_new_mode}, {"_callnewh", c_callnewh},
    {"_libm_sse2_sin_precise", m_sin}, {"_libm_sse2_cos_precise", m_cos}, {"_libm_sse2_sqrt_precise", m_sqrt},
    {"_libm_sse2_log10_precise", m_log10}, {"_libm_sse2_pow_precise", m_pow}, {"floor", m_floor},
    {"__setusermatherr", m_setusermatherr}, {"_except1", m_except1},
    {"exit", r_exit}, {"_exit", r__exit}, {"_cexit", r_cexit}, {"_c_exit", r_c_exit},
    {"_initterm", r_initterm}, {"_initterm_e", r_initterm_e}, {"_crt_atexit", r_crt_atexit},
    {"_register_onexit_function", r_register_onexit_function}, {"_initialize_onexit_table", r_initialize_onexit_table},
    {"_get_narrow_winmain_command_line", r_get_narrow_winmain_command_line},
    {"_register_thread_local_exe_atexit_callback", r_zero}, {"_configure_narrow_argv", r_zero},
    {"_set_app_type", r_zero}, {"_initialize_narrow_environment", r_zero}, {"_configthreadlocale", r_zero},
    {"_seh_filter_exe", r_seh_filter_exe}, {"_controlfp_s", r_controlfp_s}, {"terminate", r_terminate},
    {"_errno", r_errno}, {"_invalid_parameter_noinfo", r_invalid_parameter_noinfo},
    {"_invalid_parameter_noinfo_noreturn", r_invalid_parameter_noinfo_noreturn}, {"__p__commode", r_p_commode},
    {"_except_handler4_common", v_nop_handler}, {"__CxxFrameHandler3", v_nop_handler},
    {"_CxxThrowException", v_CxxThrowException}, {"__std_terminate", v_std_terminate},
    {"__std_exception_copy", v_std_exception_copy}, {"__std_exception_destroy", v_std_exception_destroy},
    {"__telemetry_main_invoke_trigger", r_zero}, {"__telemetry_main_return_trigger", r_zero},
    {"__vcrt_InitializeCriticalSectionEx", v_vcrt_InitializeCriticalSectionEx},
    {"_time64", t_time64}, {"srand", u_srand}, {"rand", u_rand},
    {"_mkdir", f_mkdir}, {"_unlink", f_unlink}, {"_chdir", f_chdir}, {"_lock_file", f_lock_file}, {"_unlock_file", f_lock_file},
    {"fclose", s_fclose}, {"fflush", s_fflush}, {"fgetc", s_fgetc}, {"fputc", s_fputc}, {"ungetc", s_ungetc},
    {"fwrite", s_fwrite}, {"fgetpos", s_fgetpos}, {"fsetpos", s_fsetpos}, {"_fseeki64", s_fseeki64},
    {"setvbuf", s_setvbuf}, {"_set_fmode", s_set_fmode}, {"_get_stream_buffer_pointers", s_get_stream_buffer_pointers},
    {"__stdio_common_vsprintf", s_common_vsprintf}, {"__stdio_common_vsscanf", s_common_vsscanf},
    {NULL, NULL},
};
