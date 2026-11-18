// Host platform layer.
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

uint8_t *plat_reserve_arena(void) {
    // reserve 4 GB + 64 KB; committed on demand by plat_commit and by the fault handler below
    return VirtualAlloc((void *)(uintptr_t)ARENA_BASE, (1ull << 32) + 0x10000, MEM_RESERVE, PAGE_NOACCESS);
}
void plat_commit(void *p, size_t n) { VirtualAlloc(p, n, MEM_COMMIT, PAGE_READWRITE); }

extern uint8_t *MEM;
static LONG CALLBACK commit_on_fault(EXCEPTION_POINTERS *e) {
    // guest code touching never-committed arena memory (e.g. a stray null read): commit it zeroed
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    uint8_t *a = (uint8_t *)e->ExceptionRecord->ExceptionInformation[1];
    if (!MEM || a < MEM || a >= MEM + (1ull << 32) + 0x10000) return EXCEPTION_CONTINUE_SEARCH;
    uint8_t *pg = (uint8_t *)((uintptr_t)a & ~(uintptr_t)0xffff);
    if (!VirtualAlloc(pg, 0x10000, MEM_COMMIT, PAGE_READWRITE)) return EXCEPTION_CONTINUE_SEARCH;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void plat_mutex_init(plat_mutex *m) {
    m->p = malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(m->p);
}
void plat_mutex_lock(plat_mutex *m) { EnterCriticalSection(m->p); }
void plat_mutex_unlock(plat_mutex *m) { LeaveCriticalSection(m->p); }
void *plat_aligned_alloc(size_t align, size_t n) { return _aligned_malloc(n, align); }
void plat_aligned_free(void *p) { _aligned_free(p); }

typedef struct { void (*fn)(void *); void *arg; } ThreadStart;
static DWORD WINAPI thread_tramp(LPVOID p) {
    ThreadStart t = *(ThreadStart *)p;
    free(p);
    t.fn(t.arg);
    return 0;
}
int plat_thread_start(void (*fn)(void *), void *arg) {
    ThreadStart *t = malloc(sizeof *t);
    t->fn = fn; t->arg = arg;
    HANDLE h = CreateThread(NULL, 64u << 20, thread_tramp, t, 0, NULL);
    if (!h) return -1;
    CloseHandle(h);
    return 0;
}
static void host_sleep_ms(unsigned ms) { Sleep(ms); }
static uint64_t host_ticks_us(void) {
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (uint64_t)(t.QuadPart * 1000000.0 / f.QuadPart);
}
void plat_init(int argc, char **argv) {
    (void)argc; (void)argv;
    AddVectoredExceptionHandler(1, commit_on_fault);
    if (GetFileAttributesA("font.bmp") != INVALID_FILE_ATTRIBUTES) return;
    // run from the folder holding the executable, like a double-clicked Windows program
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof path);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') n--;
    if (n) { path[n] = 0; SetCurrentDirectoryA(path); }
}

#else  // POSIX / macOS
#include <signal.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

uint8_t *plat_reserve_arena(void) {
    // at the fixed ARENA_BASE: rewritten code converts guest <-> host pointers with plain arithmetic
    void *want = (void *)(uintptr_t)ARENA_BASE;
#ifdef __APPLE__
    // mmap treats the address only as a hint; a fixed mach allocation fails instead of replacing a mapping
    mach_vm_address_t a = (mach_vm_address_t)ARENA_BASE;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &a, (1ull << 32) + 0x10000, VM_FLAGS_FIXED);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "guest arena: address %p is not free (%d)\n", want, kr); return NULL; }
    return want;
#else
    void *p = mmap(want, (1ull << 32) + 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || p != want) { fprintf(stderr, "guest arena not at %p\n", want); return NULL; }
    return p;
#endif
}
void plat_commit(void *p, size_t n) { (void)p; (void)n; }  // anonymous mapping is committed lazily

void plat_mutex_init(plat_mutex *m) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m->m, &a);
}
void plat_mutex_lock(plat_mutex *m) { pthread_mutex_lock(&m->m); }
void plat_mutex_unlock(plat_mutex *m) { pthread_mutex_unlock(&m->m); }
void *plat_aligned_alloc(size_t align, size_t n) {
    void *p = NULL;
    return posix_memalign(&p, align, n) ? NULL : p;
}
void plat_aligned_free(void *p) { free(p); }

typedef struct { void (*fn)(void *); void *arg; } ThreadStart;
static void *thread_tramp(void *p) {
    ThreadStart t = *(ThreadStart *)p;
    free(p);
    t.fn(t.arg);
    return NULL;
}
int plat_thread_start(void (*fn)(void *), void *arg) {
    ThreadStart *t = malloc(sizeof *t);
    t->fn = fn; t->arg = arg;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 64u << 20);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    int r = pthread_create(&th, &a, thread_tramp, t);
    pthread_attr_destroy(&a);
    return r ? -1 : 0;
}
static void host_sleep_ms(unsigned ms) { usleep(ms * 1000u); }
static uint64_t host_ticks_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
void rt_dump_stats(void);
void sdl_debug_snapshot(const char *path);
extern uint8_t *MEM;
void rt_log(const char *fmt, ...);
static void dump_mem(void) {
    // GRX_DUMP="addr:len,addr:len" (hex) -> hex dump into grx.log
    const char *p = getenv("GRX_DUMP");
    while (p && *p) {
        unsigned a = 0, n = 0;
        int k = 0;
        if (sscanf(p, "%x:%x%n", &a, &n, &k) < 2) break;
        p += k;
        if (*p == ',') p++;
        for (unsigned o = 0; o < n; o += 16) {
            char line[128];
            int m = snprintf(line, sizeof line, "%08x:", a + o);
            for (unsigned j = 0; j < 16 && o + j < n; j += 4)
                m += snprintf(line + m, sizeof line - m, " %08x", *(uint32_t *)(MEM + a + o + j));
            rt_log("%s", line);
        }
    }
}
static void on_alarm(int sig) {
    (void)sig;
    dump_mem();
    rt_dump_stats();
    sdl_debug_snapshot("headless_frame.bmp");
    _exit(0);
}
void plat_init(int argc, char **argv) {
    (void)argc; (void)argv;
    if (getenv("GRX_HEADLESS")) signal(SIGALRM, on_alarm);
    // started from inside the game folder (it has font.bmp): stay there; otherwise use the binary's folder
    if (access("font.bmp", F_OK) == 0) return;
    char path[4096];
#ifdef __APPLE__
    uint32_t sz = sizeof path;
    if (_NSGetExecutablePath(path, &sz) != 0) return;
#else
    ssize_t k = readlink("/proc/self/exe", path, sizeof path - 1);
    if (k <= 0) return;
    path[k] = 0;
#endif
    char *s = strrchr(path, '/');
    if (!s) return;
    *s = 0;
    // inside a macOS .app bundle (X.app/Contents/MacOS/bin): use the folder holding the bundle
    char *app = strstr(path, ".app/Contents/MacOS");
    if (app) {
        *app = 0;
        s = strrchr(path, '/');
        if (s) *s = 0;
    }
    if (chdir(path) != 0) fprintf(stderr, "cannot chdir to %s\n", path);
}
#endif

void plat_message_box(const char *title, const char *msg) {
    if (getenv("GRX_HEADLESS")) return;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL);
}

// ------------------------------------------------------------------ clock (real, or virtual in deterministic mode)
// GRX_DET=1: time only advances when the game waits (Sleep/SDL_Delay), presents a frame (+1/60 s, see
// shim_sdl.c) or reads the clock (+1 us per read, so polling loops terminate). Every run with the same input
// script then executes exactly the same instructions, which is what the comparison harness relies on.
int plat_det = -1;
static uint64_t vclock_us = 1000000;
int plat_is_det(void) {
    if (plat_det < 0) { const char *e = getenv("GRX_DET"); plat_det = e && e[0] == '1'; }
    return plat_det;
}
uint64_t plat_ticks_us(void) { return plat_is_det() ? vclock_us++ : host_ticks_us(); }
void plat_sleep_ms(unsigned ms) { if (plat_is_det()) vclock_us += 1000ull * ms; else host_sleep_ms(ms); }
void plat_advance_us(uint64_t us) { if (plat_is_det()) vclock_us += us; }
void plat_wall_time(int64_t *sec, long *nsec) {
    if (plat_is_det()) {   // 2018-01-06 12:00:00 UTC (GeneRally 1.2d release day) + virtual time
        *sec = 1515240000 + (int64_t)(vclock_us / 1000000);
        *nsec = (long)(vclock_us % 1000000) * 1000;
        return;
    }
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    *sec = ts.tv_sec;
    *nsec = ts.tv_nsec;
}
