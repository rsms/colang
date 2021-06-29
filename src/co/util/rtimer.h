#pragma once
ASSUME_NONNULL_BEGIN

typedef struct RTimer {
  struct rusage ru;
  u64           nstime;
} RTimer;

void rtimer_start(RTimer* rt);
u64 rtimer_duration(RTimer* rt);
Str rtimer_duration_str(RTimer* rt, Str s);
void rtimer_log(RTimer* rt, const char* fmt, ...);

ASSUME_NONNULL_END
