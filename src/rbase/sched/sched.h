#pragma once
ASSUME_NONNULL_BEGIN

// Main scheduling concepts:
typedef struct T T; // Task      (coroutine; "g" in Go parlance)
typedef struct M M; // Machine   (OS thread)
typedef struct P P; // Processor (execution resource required to execute a T)
// M must have an associated P to execute T,
// however a M can be blocked or in a syscall w/o an associated P.

void sched_init();       // initialize the scheduler
// void sched_maincancel(); // cancel the main function; returns when all tasks has ended

// Scheduler entry point. fn is the main coroutine body. Never returns.
void noreturn sched_main(void(*fn)(void));

ASSUME_NONNULL_END
