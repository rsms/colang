#pragma once
ASSUME_NONNULL_BEGIN

// Main scheduling concepts:
typedef struct T T; // Task      (coroutine; "g" in Go parlance)
typedef struct M M; // Machine   (OS thread)
typedef struct P P; // Processor (execution resource required to execute a T)
// M must have an associated P to execute T,
// however a M can be blocked or in a syscall w/o an associated P.

typedef void(*EntryFun)(uintptr_t arg1);

// Scheduler entry point. fn is the main coroutine body. Never returns.
void _Noreturn sched_main(EntryFun fn, uintptr_t arg1);

// sched_spawn schedules a new coroutine.
// Returns 0 on success and -1 on error, in which case errno is set.
int sched_spawn(EntryFun fn, uintptr_t arg1, void* nullable stackmem, size_t stacksize);

#define t_spawn(fn, arg1) \
  sched_spawn(fn, arg1, /*stackmem*/NULL, /*stacksize*/0);

#define t_spawn_custom(fn, arg1, stackmem, stacksize) \
  sched_spawn(fn, arg1, stackmem, stacksize);

void t_yield();


ASSUME_NONNULL_END
