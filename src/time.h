#pragma once
BEGIN_INTERFACE

// unixtime stores the number of seconds + nanoseconds since Jan 1 1970 00:00:00 UTC
// at *sec and *nsec
error unixtime(i64* sec, u64* nsec);

// nanotime returns nanoseconds measured from an undefined point in time.
// It uses the most high-resolution, low-latency clock available on the system.
// u64 is enough to express 584 years in nanoseconds.
u64 nanotime();

// fmtduration appends human-readable time duration to buf, including a null terminator.
// Returns number of bytes written, excluding the null terminator.
usize fmtduration(char buf[25], u64 duration_ns);

// microsleep sleeps for some number of microseconds.
// Returns amount of time remaining if the thread was interrupted
// or 0 if the thread slept for at least microseconds.
u64 microsleep(u64 microseconds);

// time_init initializes the time library
error time_init();

// logtime -- measure real time spent between two points of execution.
//   TimeLabel logtime_start(const char* label)
//   void      logtime_end(const TimeLabel)
//   void      logtime_scope(const char* label)
//
typedef struct { const char* label; u64 ns; } TimeLabel;
inline static TimeLabel logtime_start(const char* label) {
  return (TimeLabel){label,nanotime()}; }
#define logtime_end(t) _logtime_end(&(t))
void _logtime_end(const TimeLabel* t);
#if __has_attribute(__cleanup__)
  #define logtime_scope(label)                                              \
    UNUSED TimeLabel logtime__ __attribute__((__cleanup__(_logtime_end))) = \
    logtime_start(label)
#else
  #define logtime_scope(label) _logtime_end(logtime_start(label))
#endif


END_INTERFACE
