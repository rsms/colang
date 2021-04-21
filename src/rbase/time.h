#pragma once
ASSUME_NONNULL_BEGIN

// nanotime returns nanoseconds measured from an undefined point in time.
// It uses the most high-resolution, low-latency clock available on the system.
u64 nanotime();

// msleep sleeps for some number of milliseconds
// It may sleep for less time if a signal was delivered.
// Returns 0 when sleept for the requested time, -1 when interrupted.
int msleep(u64 milliseconds);

// fmtduration appends human-readable time duration to buf
int fmtduration(char* buf, int bufsize, u64 duration_ns);


ASSUME_NONNULL_END
