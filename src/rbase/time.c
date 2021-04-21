#include "rbase.h"

#include <time.h>
#include <sys/time.h>
#if defined __APPLE__
#include <mach/mach_time.h>
#endif

// msleep sleeps for some number of milliseconds
// It may sleep for less time if a signal was delivered.
// Returns 0 when sleept for the requested time, -1 when interrupted.
int msleep(u64 milliseconds) {
  const u64 sec = milliseconds / 1000;
  struct timespec rqtp = {
    .tv_sec  = (long)sec,
    .tv_nsec = (milliseconds - (sec * 1000)) * 1000000,
  };
  return nanosleep(&rqtp, NULL);
}

int fmtduration(char* buf, int bufsize, u64 duration_ns) {
  const char* fmt = "%.0fns";
  double d = duration_ns;
  if (duration_ns >= 1000000000) {
    d /= 1000000000;
    fmt = "%.1fs";
  } else if (duration_ns >= 1000000) {
    d /= 1000000;
    fmt = "%.1fms";
  } else if (duration_ns >= 1000) {
    d /= 1000;
    fmt = "%.0fus";
  }
  return snprintf(buf, bufsize, fmt, d);
}


// nanotime returns nanoseconds measured from an undefined point in time
// u64 nanotime() {
//   struct timespec t;
//   clock_gettime(CLOCK_MONOTONIC, &t);
//   return ((u64)t.tv_sec)*1000000000 + (u64)t.tv_nsec;
// }

static inline __attribute__((always_inline))
u64 _nanotime(void) {
#if defined(__MACH__)
  static mach_timebase_info_data_t ti;
  static int once = 0;
  if (!once) {
    once = 1;
    mach_timebase_info(&ti);
  }
  u64 t = mach_absolute_time();
  return (t * ti.numer) / ti.denom;
#elif defined(CLOCK_MONOTONIC)
  struct timespec ts;
  #ifdef NDEBUG
  clock_gettime(CLOCK_MONOTONIC, &ts);
  #else
  assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
  #endif
  return ((u64)(ts.tv_sec) * 1000000000) + ts.tv_nsec;
// #elif (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
  // TODO: QueryPerformanceCounter
#else
  struct timeval tv;
  #ifdef NDEBUG
  gettimeofday(&tv, nullptr);
  #else
  assert(gettimeofday(&tv, nullptr) == 0);
  #endif
  return ((u64)(tv.tv_sec) * 1000000000) + ((u64)(tv.tv_usec) * 1000);
#endif
}

// #define USE_RDTSC_CACHE

u64 nanotime() {

#ifdef USE_RDTSC_CACHE
  #if (defined(__GNUC__) || defined(__clang__)) && \
      (defined(__i386__) || defined(__x86_64__))
    u32 low;
    u32 high;
    __asm__ __volatile__ ("rdtsc" : "=a" (low), "=d" (high));
    u64 tsc = ((u64)high << 32) | low;
    #define HAS_RDTSC
  #elif (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
    u64 tsc = __rdtsc();
    #define HAS_RDTSC
  #elif (defined(__SUNPRO_CC) && (__SUNPRO_CC >= 0x5100) && \
        ( defined(__i386) || defined(__amd64) || defined(__x86_64) ))
    union { u64 u64val; u32 u32val[2]; } tscu;
    asm("rdtsc" : "=a" (tsc.u32val [0]), "=d" (tsc.u32val [1]));
    u64 tsc = tsc.u64val;
    #define HAS_RDTSC
  #endif
#endif /* USE_RDTSC_CACHE */

#if defined(HAS_RDTSC)
  // RDTSC is unreliable as a time measurement device itself, but here
  // we use it merely to optimize number of _nanotime samples.

  constexpr u64 CLOCK_PRECISION = 1000; // 1ms

  static i64 last_tsc = -1;
  static i64 last_time = -1;

  static sync_once_flag once;
  sync_once(&once, {
    last_tsc = tsc;
    last_time = _nanotime();
  });

  if (tsc - last_tsc <= (CLOCK_PRECISION / 2) && tsc >= last_tsc) {
    return last_time + ((tsc - g_clockbuf.last_tsc) / (1000000 / CLOCK_PRECISION));
  }

  if (tsc >= last_tsc && tsc - last_tsc <= (CLOCK_PRECISION / 2)) {
    // less than 1/2 ms since we sampled _nanotime
    return last_time + ((tsc - last_tsc) / (1000000 / CLOCK_PRECISION));
  }

  last_tsc = tsc;
  last_time = _nanotime();
  return last_time;
#else
  return _nanotime();
#endif
}

