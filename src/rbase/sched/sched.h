#pragma once
ASSUME_NONNULL_BEGIN

// Main scheduling concepts:
typedef struct T T; // Task      (coroutine; "g" in Go parlance)
typedef struct M M; // Machine   (OS thread)
typedef struct P P; // Processor (execution resource required to execute a T)
// M must have an associated P to execute T,
// however a M can be blocked or in a syscall w/o an associated P.

typedef void(*EntryFun)(void);

void sched_init();       // initialize the scheduler
// void sched_maincancel(); // cancel the main function; returns when all tasks has ended

// Scheduler entry point. fn is the main coroutine body. Never returns.
void noreturn sched_main(EntryFun);

// newproc schedules a new coroutine.
// Returns 0 on success and -1 on error, in which case errno is set.
int newproc(
  EntryFun       fn,
  void* nullable argp,
  u32            argsize,
  void* nullable stackmem,
  size_t         stacksize );

#define t_spawn(fn) \
  newproc(fn, /*argptr*/NULL, /*argc*/0, /*stackmem*/NULL, /*stacksize*/0);

#define t_spawn_custom(fn, stackmem, stacksize) \
  newproc(fn, /*argptr*/NULL, /*argc*/0, stackmem, stacksize);

ASSUME_NONNULL_END
