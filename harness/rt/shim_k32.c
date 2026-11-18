// KERNEL32 / USER32 / SHELL32 imports, plus the kernel32 functions grnet resolves with GetProcAddress.
#include "rt.h"
#include "check.h"
#include "plat.h"
#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>
#endif

#define SHIM(n) static void n(CPU *c)

// ------------------------------------------------------------------ handles
enum { H_FREE, H_FIND, H_EVENT, H_FILE, H_THREAD, H_MODULE };
typedef struct {
    int kind;
    volatile int signaled;
    int manual;
    FILE *f;
#ifdef _WIN32
    HANDLE fh;
    WIN32_FIND_DATAA wfd;
    int first;
#else
    DIR *dir;
#endif
    char dirpath[1024];
    char pattern[256];
} Handle;
static Handle htab[256];
static plat_mutex hmx;
static int hmx_init;
#define HBASE 0x100

static void hlock(void) {
    if (!hmx_init) { plat_mutex_init(&hmx); hmx_init = 1; }
    plat_mutex_lock(&hmx);
}
static uint32_t h_new(int kind) {
    hlock();
    for (int i = 1; i < 256; i++)
        if (htab[i].kind == H_FREE) {
            memset(&htab[i], 0, sizeof htab[i]);
            htab[i].kind = kind;
            plat_mutex_unlock(&hmx);
            return HBASE + 4 * i;
        }
    plat_mutex_unlock(&hmx);
    return 0;
}
static Handle *h_get(uint32_t h, int kind) {
    if (h < HBASE + 4 || h >= HBASE + 4 * 256 || (h - HBASE) % 4) return NULL;
    Handle *x = &htab[(h - HBASE) / 4];
    return x->kind == kind ? x : NULL;
}

// ------------------------------------------------------------------ critical sections
// guest CRITICAL_SECTION (24 bytes): we keep a host mutex index in LockSemaphore (+0x10)
#define CS_MAGIC 0x43534d58u
static plat_mutex cs_tab[512];
static int cs_n;
static plat_mutex *cs_get(uint32_t cs) {
    if (G32(cs + 0x14) != CS_MAGIC) {
        hlock();
        if (G32(cs + 0x14) != CS_MAGIC) {
            if (cs_n >= 512) rt_fatal(NULL, "too many critical sections");
            plat_mutex_init(&cs_tab[cs_n]);
            G32(cs + 0x10) = (uint32_t)cs_n++;
            G32(cs + 0x14) = CS_MAGIC;
        }
        plat_mutex_unlock(&hmx);
    }
    return &cs_tab[G32(cs + 0x10)];
}
SHIM(k_InitializeCriticalSection) { G32(ARG(0) + 0x14) = 0; cs_get(ARG(0)); ret_std(c, 0, 1); }
SHIM(k_InitializeCriticalSectionEx) { G32(ARG(0) + 0x14) = 0; cs_get(ARG(0)); ret_std(c, 1, 3); }
SHIM(k_EnterCriticalSection) { plat_mutex_lock(cs_get(ARG(0))); ret_std(c, 0, 1); }
SHIM(k_LeaveCriticalSection) { plat_mutex_unlock(cs_get(ARG(0))); ret_std(c, 0, 1); }
SHIM(k_DeleteCriticalSection) { ret_std(c, 0, 1); }

// ------------------------------------------------------------------ events (polling)
SHIM(k_CreateEventW) {
    uint32_t h = h_new(H_EVENT);
    Handle *e = h_get(h, H_EVENT);
    e->manual = ARG(1) != 0;
    e->signaled = ARG(2) != 0;
    ret_std(c, h, 4);
}
SHIM(k_SetEvent) { Handle *e = h_get(ARG(0), H_EVENT); if (e) e->signaled = 1; ret_std(c, e != NULL, 1); }
SHIM(k_ResetEvent) { Handle *e = h_get(ARG(0), H_EVENT); if (e) e->signaled = 0; ret_std(c, e != NULL, 1); }
SHIM(k_WaitForSingleObjectEx) {
    Handle *e = h_get(ARG(0), H_EVENT);
    uint32_t ms = ARG(1);
    uint64_t end = plat_ticks_us() + (uint64_t)ms * 1000;
    uint32_t r = 0x102;  // WAIT_TIMEOUT
    while (e) {
        if (e->signaled) { if (!e->manual) e->signaled = 0; r = 0; break; }
        if (ms != 0xffffffffu && plat_ticks_us() >= end) break;
        plat_sleep_ms(1);
    }
    if (!e) r = 0;
    ret_std(c, r, 3);
}
SHIM(k_CloseHandle) {
    uint32_t h = ARG(0);
    hlock();
    if (h >= HBASE + 4 && h < HBASE + 4 * 256) {
        Handle *x = &htab[(h - HBASE) / 4];
        if (x->kind == H_FILE && x->f) fclose(x->f);
        if (x->kind != H_MODULE) x->kind = H_FREE;
    }
    plat_mutex_unlock(&hmx);
    ret_std(c, 1, 1);
}

// ------------------------------------------------------------------ process / misc
SHIM(k_UnhandledExceptionFilter) { ret_std(c, 0, 1); }
SHIM(k_SetUnhandledExceptionFilter) { ret_std(c, 0, 1); }
SHIM(k_GetCurrentProcess) { ret_std(c, 0xffffffffu, 0); }
SHIM(k_TerminateProcess) { rt_log("TerminateProcess(%u)", ARG(1)); chk_exit((int)ARG(1)); }
SHIM(k_IsProcessorFeaturePresent) { uint32_t f = ARG(0); ret_std(c, f == 6 || f == 10, 1); }
SHIM(k_IsDebuggerPresent) { ret_std(c, 0, 0); }
SHIM(k_GetStartupInfoW) { memset(GP(ARG(0)), 0, 68); G32(ARG(0)) = 68; ret_std(c, 0, 1); }
SHIM(k_GetCurrentProcessId) { ret_std(c, 0x1000, 0); }
SHIM(k_GetCurrentThreadId) { ret_std(c, 0x2000 + c->id, 0); }
SHIM(k_InitializeSListHead) { memset(GP(ARG(0)), 0, 8); ret_std(c, 0, 1); }
SHIM(k_lstrlenA) { ret_std(c, ARG(0) ? (uint32_t)strlen(GS(ARG(0))) : 0, 1); }
SHIM(k_QueryPerformanceCounter) { G64(ARG(0)) = plat_ticks_us(); ret_std(c, 1, 1); }
SHIM(k_QueryPerformanceFrequency) { G64(ARG(0)) = 1000000; ret_std(c, 1, 1); }
SHIM(k_GetTickCount) { ret_std(c, (uint32_t)(plat_ticks_us() / 1000), 0); }
SHIM(k_Sleep) { plat_sleep_ms(ARG(0)); ret_std(c, 0, 1); }

static uint32_t cmdline;
SHIM(k_GetCommandLineA) { if (!cmdline) cmdline = gstrdup("GR12.exe"); ret_std(c, cmdline, 0); }

static uint64_t filetime_now(void) {
    // 100 ns intervals since 1601-01-01
    int64_t sec; long nsec;
    plat_wall_time(&sec, &nsec);
    return (uint64_t)sec * 10000000ull + (uint64_t)nsec / 100 + 116444736000000000ull;
}
SHIM(k_GetSystemTimeAsFileTime) { G64(ARG(0)) = filetime_now(); ret_std(c, 0, 1); }
SHIM(k_GetSystemTime) {
    int64_t sec; long nsec;
    plat_wall_time(&sec, &nsec);
    time_t t = (time_t)sec;
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    uint32_t p = ARG(0);
    G16(p + 0) = (uint16_t)(tmv.tm_year + 1900);
    G16(p + 2) = (uint16_t)(tmv.tm_mon + 1);
    G16(p + 4) = (uint16_t)tmv.tm_wday;
    G16(p + 6) = (uint16_t)tmv.tm_mday;
    G16(p + 8) = (uint16_t)tmv.tm_hour;
    G16(p + 10) = (uint16_t)tmv.tm_min;
    G16(p + 12) = (uint16_t)tmv.tm_sec;
    G16(p + 14) = (uint16_t)(nsec / 1000000);
    ret_std(c, 0, 1);
}
// GetDateFormatA(locale, flags, SYSTEMTIME*, format, buf, cch): short date, d.M.yyyy
SHIM(k_GetDateFormatA) {
    uint32_t st = ARG(2), buf = ARG(4), cch = ARG(5);
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%d.%d.%04d", G16(st + 6), G16(st + 2), G16(st + 0));
    uint32_t n = (uint32_t)strlen(tmp) + 1;
    if (cch == 0) { ret_std(c, n, 6); return; }
    if (n > cch) { ret_std(c, 0, 6); return; }
    memcpy(GP(buf), tmp, n);
    ret_std(c, n, 6);
}
// CompareStringA(locale, flags, s1, n1, s2, n2) -> 1 less, 2 equal, 3 greater
SHIM(k_CompareStringA) {
    uint32_t flags = ARG(1);
    const char *a = GS(ARG(2)), *b = GS(ARG(4));
    int na = (int)ARG(3), nb = (int)ARG(5);
    if (na < 0) na = (int)strlen(a);
    if (nb < 0) nb = (int)strlen(b);
    int i = 0, r = 0;
    for (; i < na && i < nb; i++) {
        int x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (flags & 1) { x = tolower(x); y = tolower(y); }
        if (x != y) { r = x < y ? -1 : 1; break; }
    }
    if (!r && na != nb) r = na < nb ? -1 : 1;
    ret_std(c, r < 0 ? 1 : r > 0 ? 3 : 2, 6);
}

// ------------------------------------------------------------------ modules
#define HMOD_K32 0x7c800000u
#define HMOD_WS2 0x71ab0000u
SHIM(k_GetModuleHandleW) { ret_std(c, ARG(0) ? HMOD_K32 : IMAGE_BASE, 1); }
SHIM(k_GetModuleHandleA) { ret_std(c, ARG(0) ? HMOD_K32 : IMAGE_BASE, 1); }
SHIM(k_LoadLibraryA) {
    const char *n = GS(ARG(0));
    ret_std(c, (n[0] == 'w' || n[0] == 'W') ? HMOD_WS2 : HMOD_K32, 1);
}
SHIM(k_GetProcAddress) {
    uint32_t name = ARG(1);
    uint32_t t = name > 0xffff ? rt_thunk_for(GS(name)) : 0;
    if (!t && name > 0xffff) rt_log("GetProcAddress: no shim for %s (name at %08x, module %08x)", GS(name), name, ARG(0));
    ret_std(c, t, 2);
}
SHIM(k_GetModuleFileNameA) {
    const char *p = ".\\GR12.exe";
    uint32_t buf = ARG(1), n = ARG(2), len = (uint32_t)strlen(p);
    if (n == 0) { ret_std(c, 0, 3); return; }
    if (len >= n) len = n - 1;
    memcpy(GP(buf), p, len);
    G8(buf + len) = 0;
    ret_std(c, len, 3);
}

// ------------------------------------------------------------------ threads (grnet's network thread)
typedef struct { uint32_t start, param; } GThread;
static void thread_main(void *p) {
    GThread t = *(GThread *)p;
    free(p);
    CPU *c = rt_new_cpu(THREAD_STACK);
    uint32_t a = t.param;
    guest_call(c, t.start, 1, &a);
    rt_free_cpu(c);
}
SHIM(k_CreateThread) {
    GThread *t = malloc(sizeof *t);
    t->start = ARG(2);
    t->param = ARG(3);
    uint32_t tidp = ARG(5);
    int r = plat_thread_start(thread_main, t);
    if (tidp) G32(tidp) = 0x3000;
    ret_std(c, r == 0 ? h_new(H_THREAD) : 0, 6);
}

// ------------------------------------------------------------------ files (grnet log)
SHIM(k_CreateFileA) {
    char p[1024];
    host_path(GS(ARG(0)), p, sizeof p);
    uint32_t access = ARG(1), disp = ARG(4);
    const char *mode = (access & 0x40000000u) ? (disp == 4 /*OPEN_ALWAYS*/ ? "ab" : "wb") : "rb";
    FILE *f = fopen(p, mode);
    if (!f) { ret_std(c, 0xffffffffu, 7); return; }
    uint32_t h = h_new(H_FILE);
    h_get(h, H_FILE)->f = f;
    ret_std(c, h, 7);
}
SHIM(k_WriteFile) {
    Handle *x = h_get(ARG(0), H_FILE);
    uint32_t n = 0;
    if (x && x->f) n = (uint32_t)fwrite(GP(ARG(1)), 1, ARG(2), x->f);
    if (ARG(3)) G32(ARG(3)) = n;
    ret_std(c, x != NULL, 5);
}
SHIM(k_FlushFileBuffers) { Handle *x = h_get(ARG(0), H_FILE); if (x && x->f) fflush(x->f); ret_std(c, 1, 1); }

// GetPrivateProfileStringA(section, key, default, buf, size, file)
SHIM(k_GetPrivateProfileStringA) {
    const char *sec = ARG(0) ? GS(ARG(0)) : NULL, *key = ARG(1) ? GS(ARG(1)) : NULL;
    const char *def = ARG(2) ? GS(ARG(2)) : "";
    uint32_t buf = ARG(3), size = ARG(4);
    char path[1024], line[1024], val[1024];
    host_path(GS(ARG(5)), path, sizeof path);
    snprintf(val, sizeof val, "%s", def);
    FILE *f = (sec && key) ? fopen(path, "r") : NULL;
    int insec = 0;
    while (f && fgets(line, sizeof line, f)) {
        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        char *e = s + strlen(s);
        while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
        if (*s == ';' || *s == '#' || !*s) continue;
        if (*s == '[') {
            char *r = strchr(s, ']');
            if (r) *r = 0;
#ifdef _WIN32
            insec = !_stricmp(s + 1, sec);
#else
            insec = !strcasecmp(s + 1, sec);
#endif
            continue;
        }
        char *eq = strchr(s, '=');
        if (!insec || !eq) continue;
        *eq = 0;
        char *k = s, *ke = eq;
        while (ke > k && isspace((unsigned char)ke[-1])) *--ke = 0;
        char *v = eq + 1;
        while (isspace((unsigned char)*v)) v++;
#ifdef _WIN32
        if (!_stricmp(k, key)) { snprintf(val, sizeof val, "%s", v); break; }
#else
        if (!strcasecmp(k, key)) { snprintf(val, sizeof val, "%s", v); break; }
#endif
    }
    if (f) fclose(f);
    uint32_t n = (uint32_t)strlen(val);
    if (size == 0) { ret_std(c, 0, 6); return; }
    if (n >= size) n = size - 1;
    memcpy(GP(buf), val, n);
    G8(buf + n) = 0;
    ret_std(c, n, 6);
}

// ------------------------------------------------------------------ directory search
// WIN32_FIND_DATAA: attrs 0, times 4/0xc/0x14, sizeHigh 0x1c, sizeLow 0x20, cFileName 0x2c, cAlternateFileName 0x130
static void fill_find(uint32_t fd, const char *dir, const char *name) {
    memset(GP(fd), 0, 0x140);
    char full[2048];
    snprintf(full, sizeof full, "%s%c%s", dir, PLAT_SEP, name);
    struct stat st;
    uint32_t attr = 0x80;
    uint64_t size = 0;
    if (stat(full, &st) == 0) {
        if (S_ISDIR(st.st_mode)) attr = 0x10;
        else { attr = 0x20; size = (uint64_t)st.st_size; }
        uint64_t ft = (uint64_t)st.st_mtime * 10000000ull + 116444736000000000ull;
        G64(fd + 4) = ft; G64(fd + 0xc) = ft; G64(fd + 0x14) = ft;
    }
    G32(fd) = attr;
    G32(fd + 0x1c) = (uint32_t)(size >> 32);
    G32(fd + 0x20) = (uint32_t)size;
    snprintf(GS(fd + 0x2c), 260, "%s", name);
}

static int pat_match(const char *pat, const char *name) {
    if (!strcmp(pat, "*") || !strcmp(pat, "*.*")) return 1;
#ifdef _WIN32
    (void)pat; (void)name;
    return 1;  // Windows host: FindFirstFileA already filtered
#else
    return fnmatch(pat, name, FNM_CASEFOLD) == 0;
#endif
}

static int find_next(Handle *x, uint32_t fd) {
#ifdef _WIN32
    if (x->first) { x->first = 0; }
    else if (!FindNextFileA(x->fh, &x->wfd)) return 0;
    fill_find(fd, x->dirpath, x->wfd.cFileName);
    return 1;
#else
    struct dirent *de;
    while ((de = readdir(x->dir))) {
        if (pat_match(x->pattern, de->d_name)) {
            fill_find(fd, x->dirpath, de->d_name);
            return 1;
        }
    }
    return 0;
#endif
}

SHIM(k_FindFirstFileA) {
    char p[1024];
    host_path(GS(ARG(0)), p, sizeof p);
    uint32_t h = h_new(H_FIND);
    Handle *x = h_get(h, H_FIND);
    char *s = strrchr(p, PLAT_SEP);
    if (s) { *s = 0; snprintf(x->dirpath, sizeof x->dirpath, "%s", p[0] ? p : "/"); snprintf(x->pattern, sizeof x->pattern, "%s", s + 1); }
    else { snprintf(x->dirpath, sizeof x->dirpath, "."); snprintf(x->pattern, sizeof x->pattern, "%s", p); }
#ifdef _WIN32
    char q[1300];
    snprintf(q, sizeof q, "%s\\%s", x->dirpath, x->pattern);
    x->fh = FindFirstFileA(q, &x->wfd);
    x->first = 1;
    if (x->fh == INVALID_HANDLE_VALUE) { x->kind = H_FREE; ret_std(c, 0xffffffffu, 2); return; }
#else
    x->dir = opendir(x->dirpath);
    if (!x->dir) { x->kind = H_FREE; ret_std(c, 0xffffffffu, 2); return; }
#endif
    if (!find_next(x, ARG(1))) {
#ifdef _WIN32
        FindClose(x->fh);
#else
        closedir(x->dir);
#endif
        x->kind = H_FREE;
        ret_std(c, 0xffffffffu, 2);
        return;
    }
    ret_std(c, h, 2);
}
SHIM(k_FindNextFileA) {
    Handle *x = h_get(ARG(0), H_FIND);
    ret_std(c, x ? (uint32_t)find_next(x, ARG(1)) : 0, 2);
}
SHIM(k_FindClose) {
    Handle *x = h_get(ARG(0), H_FIND);
    if (x) {
#ifdef _WIN32
        FindClose(x->fh);
#else
        closedir(x->dir);
#endif
        x->kind = H_FREE;
    }
    ret_std(c, x != NULL, 1);
}
SHIM(k_MoveFileA) {
    char a[1024], b[1024];
    host_path(GS(ARG(0)), a, sizeof a);
    host_path(GS(ARG(1)), b, sizeof b);
    ret_std(c, rename(a, b) == 0, 2);
}

// ------------------------------------------------------------------ USER32 / SHELL32
// DEVMODEA: dmSize 0x24, dmFields 0x28, dmBitsPerPel 0x68, dmPelsWidth 0x6c, dmPelsHeight 0x70, dmDisplayFrequency 0x78
static int nmodes = -1;
static int mode_w[64], mode_h[64];
static void scan_modes(void) {
    if (nmodes >= 0) return;
    nmodes = 0;
    if (!SDL_WasInit(SDL_INIT_VIDEO)) SDL_InitSubSystem(SDL_INIT_VIDEO);
    int n = SDL_GetNumDisplayModes(0);
    for (int i = 0; i < n && nmodes < 64; i++) {
        SDL_DisplayMode m;
        if (SDL_GetDisplayMode(0, i, &m)) continue;
        int dup = 0;
        for (int k = 0; k < nmodes; k++) if (mode_w[k] == m.w && mode_h[k] == m.h) dup = 1;
        if (!dup) { mode_w[nmodes] = m.w; mode_h[nmodes] = m.h; nmodes++; }
    }
    // the game's own classic resolutions always work (window is scaled)
    static const int std_w[] = {640, 800, 1024, 1280}, std_h[] = {480, 600, 768, 960};
    for (int k = 0; k < 4 && nmodes < 64; k++) {
        int dup = 0;
        for (int j = 0; j < nmodes; j++) if (mode_w[j] == std_w[k] && mode_h[j] == std_h[k]) dup = 1;
        if (!dup) { mode_w[nmodes] = std_w[k]; mode_h[nmodes] = std_h[k]; nmodes++; }
    }
}
SHIM(u_EnumDisplaySettingsA) {
    uint32_t i = ARG(1), dm = ARG(2);
    scan_modes();
    if (i == 0xffffffffu) i = 0;
    if ((int)i >= nmodes) { ret_std(c, 0, 3); return; }
    G32(dm + 0x28) = 0x1c0000;
    G32(dm + 0x68) = 32;
    G32(dm + 0x6c) = (uint32_t)mode_w[i];
    G32(dm + 0x70) = (uint32_t)mode_h[i];
    G32(dm + 0x78) = 60;
    ret_std(c, 1, 3);
}
SHIM(u_MessageBoxA) {
    rt_log("MessageBox: %s: %s", ARG(2) ? GS(ARG(2)) : "", ARG(1) ? GS(ARG(1)) : "");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, ARG(2) ? GS(ARG(2)) : "", ARG(1) ? GS(ARG(1)) : "", NULL);
    ret_std(c, 1, 4);
}

static void remove_tree(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
#ifdef _WIN32
        char q[1100];
        WIN32_FIND_DATAA w;
        snprintf(q, sizeof q, "%s\\*", p);
        HANDLE h = FindFirstFileA(q, &w);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!strcmp(w.cFileName, ".") || !strcmp(w.cFileName, "..")) continue;
                snprintf(q, sizeof q, "%s\\%s", p, w.cFileName);
                remove_tree(q);
            } while (FindNextFileA(h, &w));
            FindClose(h);
        }
        _rmdir(p);
#else
        DIR *d = opendir(p);
        struct dirent *de;
        while (d && (de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char q[2048];
            snprintf(q, sizeof q, "%s/%s", p, de->d_name);
            remove_tree(q);
        }
        if (d) closedir(d);
        rmdir(p);
#endif
    } else remove(p);
}
// SHFileOperationA: the game only deletes (wFunc 3 = FO_DELETE); pFrom is a double-NUL list
SHIM(s_SHFileOperationA) {
    uint32_t op = ARG(0);
    uint32_t func = G32(op + 4), from = G32(op + 8);
    if (func == 3 && from) {
        while (G8(from)) {
            char p[1024];
            host_path(GS(from), p, sizeof p);
            remove_tree(p);
            from += (uint32_t)strlen(GS(from)) + 1;
        }
        ret_std(c, 0, 1);
        return;
    }
    rt_log("SHFileOperationA: unsupported func %u", func);
    ret_std(c, 1, 1);
}

const ShimDef shims_k32[] = {
    {"UnhandledExceptionFilter", k_UnhandledExceptionFilter},
    {"SetUnhandledExceptionFilter", k_SetUnhandledExceptionFilter},
    {"GetCurrentProcess", k_GetCurrentProcess},
    {"TerminateProcess", k_TerminateProcess},
    {"IsProcessorFeaturePresent", k_IsProcessorFeaturePresent},
    {"CloseHandle", k_CloseHandle},
    {"EnterCriticalSection", k_EnterCriticalSection},
    {"LeaveCriticalSection", k_LeaveCriticalSection},
    {"DeleteCriticalSection", k_DeleteCriticalSection},
    {"InitializeCriticalSection", k_InitializeCriticalSection},
    {"InitializeCriticalSectionEx", k_InitializeCriticalSectionEx},
    {"InitializeCriticalSectionAndSpinCount", k_InitializeCriticalSectionEx},
    {"SetEvent", k_SetEvent},
    {"ResetEvent", k_ResetEvent},
    {"WaitForSingleObjectEx", k_WaitForSingleObjectEx},
    {"CreateEventW", k_CreateEventW},
    {"GetModuleHandleW", k_GetModuleHandleW},
    {"GetModuleHandleA", k_GetModuleHandleA},
    {"GetProcAddress", k_GetProcAddress},
    {"LoadLibraryA", k_LoadLibraryA},
    {"IsDebuggerPresent", k_IsDebuggerPresent},
    {"GetStartupInfoW", k_GetStartupInfoW},
    {"GetCurrentProcessId", k_GetCurrentProcessId},
    {"GetCurrentThreadId", k_GetCurrentThreadId},
    {"GetSystemTimeAsFileTime", k_GetSystemTimeAsFileTime},
    {"InitializeSListHead", k_InitializeSListHead},
    {"GetCommandLineA", k_GetCommandLineA},
    {"lstrlenA", k_lstrlenA},
    {"GetSystemTime", k_GetSystemTime},
    {"GetDateFormatA", k_GetDateFormatA},
    {"CompareStringA", k_CompareStringA},
    {"FindClose", k_FindClose},
    {"FindNextFileA", k_FindNextFileA},
    {"FindFirstFileA", k_FindFirstFileA},
    {"QueryPerformanceCounter", k_QueryPerformanceCounter},
    {"QueryPerformanceFrequency", k_QueryPerformanceFrequency},
    {"MoveFileA", k_MoveFileA},
    {"GetTickCount", k_GetTickCount},
    {"Sleep", k_Sleep},
    {"CreateThread", k_CreateThread},
    {"GetModuleFileNameA", k_GetModuleFileNameA},
    {"GetPrivateProfileStringA", k_GetPrivateProfileStringA},
    {"CreateFileA", k_CreateFileA},
    {"WriteFile", k_WriteFile},
    {"FlushFileBuffers", k_FlushFileBuffers},
    {"EnumDisplaySettingsA", u_EnumDisplaySettingsA},
    {"MessageBoxA", u_MessageBoxA},
    {"SHFileOperationA", s_SHFileOperationA},
    {NULL, NULL},
};
