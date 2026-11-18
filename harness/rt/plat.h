// Host platform layer (POSIX/macOS and Win32).
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define PLAT_SEP '\\'
typedef struct { void *p; } plat_mutex;  // CRITICAL_SECTION*
#else
#include <pthread.h>
#define PLAT_SEP '/'
typedef struct { pthread_mutex_t m; } plat_mutex;
#endif

#define ARENA_BASE 0x10000000000ull          // host address of guest address 0 (rewrite relies on it)
uint8_t *plat_reserve_arena(void);          // 4 GB + slack, reads of untouched memory return zero
void plat_commit(void *p, size_t n);
void plat_mutex_init(plat_mutex *m);         // recursive
void plat_mutex_lock(plat_mutex *m);
void plat_mutex_unlock(plat_mutex *m);
void *plat_aligned_alloc(size_t align, size_t n);
void plat_aligned_free(void *p);
int plat_thread_start(void (*fn)(void *), void *arg);
void plat_sleep_ms(unsigned ms);
uint64_t plat_ticks_us(void);
void plat_message_box(const char *title, const char *msg);
int plat_is_det(void);                        // GRX_DET=1: deterministic virtual clock
void plat_advance_us(uint64_t us);           // advance the virtual clock (no-op in real time)
void plat_wall_time(int64_t *sec, long *nsec);   // UTC (fixed epoch + virtual time when deterministic)
