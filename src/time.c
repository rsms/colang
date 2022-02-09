#include "coimpl.h"
#include "time.h"
#include "str.h"

#ifdef CO_WITH_LIBC
  #include <errno.h>
  #include <time.h>
  #include <sys/time.h>
  #if defined __APPLE__
    #include <mach/mach_time.h>
  #endif
#endif

#ifndef NAN
  #if __has_builtin(__builtin_nanf)
    #define NAN __builtin_nanf("")
  #else
    #define NAN (0.0f/0.0f)
  #endif
#endif


error unixtime2(i64* sec, u64* nsec) {
  #ifdef CLOCK_REALTIME
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts))
      return error_from_errno(errno);
    *sec = (i64)ts.tv_sec;
    *nsec = (u64)ts.tv_nsec;
    return 0;
  #elif defined(CO_WITH_LIBC)
    struct timeval tv;
    gettimeofday(&tv, 0);
    *sec = (i64)tv.tv_sec;
    *nsec = ((u64)tv.tv_usec) * 1000;
    return 0;
  #else
    #warning TODO non-libc unixtime2
    return err_not_supported;
  #endif
}


i64 unixtime() {
  i64 sec;
  u64 nsec;
  if (unixtime2(&sec, &nsec))
    return I64_MIN;
  return sec;
}


double unixtimef() {
  i64 sec;
  u64 nsec;
  if (unixtime2(&sec, &nsec))
    return NAN;
  return (double)sec + ((double)nsec * 1e-9);
}


// nanotime returns nanoseconds measured from an undefined point in time
u64 nanotime(void) {
  #if defined(__APPLE__)
    static mach_timebase_info_data_t ti;
    static bool ti_init = false;
    if (!ti_init) {
      // note on atomicity: ok to do many times
      ti_init = true;
      UNUSED auto r = mach_timebase_info(&ti);
      assert(r == KERN_SUCCESS);
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
  #elif defined(CO_WITH_LIBC)
    struct timeval tv;
    #ifdef NDEBUG
    gettimeofday(&tv, nullptr);
    #else
    assert(gettimeofday(&tv, nullptr) == 0);
    #endif
    return ((u64)(tv.tv_sec) * 1000000000) + ((u64)(tv.tv_usec) * 1000);
  #else
    #warning TODO non-libc nanotime
    return 0;
  #endif
}


u64 microsleep(u64 microseconds) {
  #ifdef CO_WITH_LIBC
    u64 sec = microseconds / 1000000;
    struct timespec request = {
      .tv_sec  = (long)sec,
      .tv_nsec = (microseconds - sec*1000000) * 1000,
    };
    struct timespec remaining = {0};
    if (nanosleep(&request, &remaining) != 0)
      return remaining.tv_sec*1000000 + remaining.tv_nsec/1000;
  #else
    #warning TODO non-libc microsleep
  #endif
  return 0;
}


u32 fmtduration(char buf[25], u64 duration_ns) {
  // max value: "18446744073709551615.1ms\0"
  const char* unit = "ns";
  u64 d = duration_ns;
  u64 f = 0;
  if (duration_ns >= 1000000000) {
    f = d % 1000000000;
    d /= 1000000000;
    unit = "s\0";
  } else if (duration_ns >= 1000000) {
    f = d % 1000000;
    d /= 1000000;
    unit = "ms";
  } else if (duration_ns >= 1000) {
    d /= 1000;
    unit = "us\0";
  }
  u32 i = strfmt_u64(buf, d, 10);
  if (unit[0] != 'u' && unit[0] != 'n') {
    // one decimal for units larger than microseconds
    buf[i++] = '.';
    char buf2[20];
    UNUSED u32 n = strfmt_u64(buf2, f, 10);
    assert(n > 0);
    buf[i++] = buf2[0]; // TODO: round instead of effectively ceil
  }
  buf[i++] = unit[0];
  buf[i++] = unit[1];
  buf[i] = 0;
  return i;
}


void _logtime_end(const TimeLabel* t) {
  u64 timespent = nanotime() - t->ns;
  char durbuf[25];
  fmtduration(durbuf, timespent);
  log("◔ %s %s", t->label, durbuf);
}
