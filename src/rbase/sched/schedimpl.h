#pragma once

#if R_TARGET_OS_POSIX
  #include <signal.h> // sigset_t
#endif

ASSUME_NONNULL_BEGIN


// P_RUNQSIZE is the size of P.runq. Must be power-of-two (2^N)
#define P_RUNQSIZE 256 // 256 is the value Go 1.16 uses

// COMAXPROCS_MAX is the upper limit of COMAXPROCS.
// There are no fundamental restrictions on the value.
#define COMAXPROCS_MAX 256



// STACK_SYSTEM is a number of additional bytes to add to each stack below
// the usual guard area for OS-specific purposes like signal handling.
// Used on Windows and iOS because they do not use a separate stack.
#if R_TARGET_OS_WINDOWS
  #define STACK_SYSTEM  (512*sizeof(void*))
#elif R_TARGET_OS_IOS && R_TARGET_ARCH_ARM64
  #define STACK_SYSTEM  1024
#else
  #define STACK_SYSTEM  0
#endif

// STACK_MIN is the minimum size of stack used by coroutines
#define STACK_MIN  2048

// STACK_BIG: Functions that need frames bigger than this use an extra
// instruction to do the stack split check, to avoid overflow
// in case SP - framesize wraps below zero.
// This value can be no bigger than the size of the unmapped space at zero.
#define STACK_BIG  4096

// STACK_SMALL: After a stack split check the SP is allowed to be this
// many bytes below the stack guard. This saves an instruction
// in the checking sequence for tiny frames. (UNUSED)
#define STACK_SMALL  128

// STACK_GUARD_MULTIPLIER is a multiplier to apply to the default
// stack guard size. Larger multipliers are used for non-optimized
// builds that have larger stack frames. [go]
#define STACK_GUARD_MULTIPLIER  1

// The stack guard is a pointer this many bytes above the bottom of the stack
#define STACK_GUARD  ((928 * STACK_GUARD_MULTIPLIER) + STACK_SYSTEM)

// The maximum number of bytes that a chain of NOSPLIT
// functions can use.
#define STACK_LIMIT  (STACK_GUARD - STACK_SYSTEM - STACK_SMALL)

// FIXED_STACK is the minimum stack size to allocate
#define FIXED_STACK  POW2_CEIL(STACK_MIN + STACK_SYSTEM)

// FRAME_SIZE_MIN is the size of the system-reserved words at the bottom
// of a frame (just above the architectural stack pointer).
// It is zero on x86 and PtrSize on most non-x86 (LR-based) systems.
// On PowerPC it is larger, to cover three more reserved words:
// the compiler word, the link editor word, and the TOC save word.
// (In Go this is called MinFrameSize.)
#if R_TARGET_ARCH_X86
  #define FRAME_SIZE_MIN  0
#elif R_TARGET_ARCH_PPC
  #define FRAME_SIZE_MIN  (sizeof(void*)*4)
#else
  #define FRAME_SIZE_MIN  sizeof(void*)
#endif

// STACK_USES_LR ? ("usesLR" in Go)
#if FRAME_SIZE_MIN
  #define STACK_USES_LR 1
#else
  #define STACK_USES_LR 0
#endif

// STACK_ALIGN is the required alignment of the SP register.
// The stack must be at least word aligned, but some architectures require more.
// The values comes from go runtime/internal/sys/arch_*.go
#if R_TARGET_ARCH_ARM64 /* || TODO: R_TARGET_ARCH_PPC64 in target.h */
  #define STACK_ALIGN  16
#else
  // 386, x86_64,
  #define STACK_ALIGN  sizeof(void*)
#endif

// STACK_TSIZE is the memory at bottom of stack used for T
#define STACK_TSIZE  align2(sizeof(T), STACK_ALIGN)

// STACK_SIZE_DEFAULT is the stack size used when no specific (0) stack size
// is requested when creating a new coroutine.
#define STACK_SIZE_DEFAULT 1024*1024 // 1 MiB


typedef struct T T; // Task      (coroutine; "g" in Go parlance)
typedef struct M M; // Machine   (OS thread)
typedef struct P P; // Processor (execution resource required to execute a T)

typedef void(*TFun)(void);
typedef bool(*TUnlockFun)(T*,intptr_t);
typedef void(*MCallFun)(M*,T*);

typedef enum TStatus {
  // TIdle: coroutine was just allocated and has not yet been initialized
  TIdle = 0,

  // TRunnable: coroutine is on a run queue.
  // It is not currently executing user code.
  // The stack is not owned.
  TRunnable, // 1

  // TRunning: coroutine may execute user code.
  // The stack is owned by this coroutine.
  // It is not on a run queue.
  // It is assigned an M and a P (t.m and t.m.p are valid).
  TRunning, // 2

  // TSyscall: coroutine is executing a system call.
  // It is not executing user code.
  // The stack is owned by this coroutine.
  // It is not on a run queue.
  // It is assigned an M.
  TSyscall, // 3

  // TWaiting: coroutine is blocked in the runtime.
  // It is not executing user code.
  // It is not on a run queue, but should be recorded somewhere
  // (e.g., a channel wait queue) so it can be ready()d when necessary.
  // The stack is not owned *except* that a channel operation may read or
  // write parts of the stack under the appropriate channel
  // lock. Otherwise, it is not safe to access the stack after a
  // coroutine enters TWaiting (e.g., it may get moved).
  TWaiting, // 4

  // TDead: coroutine is currently unused.
  // It may be just exited, on a free list, or just being initialized.
  // It is not executing user code.
  // It may or may not have a stack allocated.
  // The T and its stack (if any) are owned by the M that is exiting the T
  // or that obtained the T from the free list.
  TDead, // 5
} TStatus;

typedef enum PStatus {
  // P status
  PIdle      = 0,
  PRunning, // 1 Only this P is allowed to change from _Prunning.
  PSyscall, // 2
  PDead,    // 3
} PStatus;

// Stack describes a Go execution stack.
// The bounds of the stack are exactly [lo, hi),
// with no implicit data structures on either side.
typedef struct Stack {
  uintptr_t lo;
  uintptr_t hi;
} Stack;

typedef struct StackFreelist {
  void*  list; // linked list of free stacks
  size_t size;   // total size of stacks in list
} StackFreelist;

// task stack memory
typedef struct TStackMem {
  void* p;
  u32   size;
} TStackMem;

// TQueue is a dequeue of Ts linked through g.schedlink.
// A T can only be on one TQueue or TList at a time.
typedef struct TQueue {
  T* head;
  T* tail;
} TQueue;

// TList is a list of Ts linked through T.schedlink.
// A T can only be on one TQueue or TList at a time.
typedef struct TList {
  T* head;
} TList;

typedef struct Note {
  // key holds:
  // a) nullptr when unused.
  // b) pointer to a sleeping M.
  // c) special internally-known value to indicate locked state.
  _Atomic(uintptr_t) key; // must be initialized to 0
} Note;

// SigSet
#if R_TARGET_OS_POSIX
  typedef sigset_t SigSet;
#else
  #error "TODO SigSet for this target"
#endif

typedef struct T {
  u64 id;     // global unique identifier
  M*  m;
  M*  lockedm;

  Stack stack;
  void* stackctx;  // execution context (stack, regs; fctx) of T or M

  //void*     stackp;    // stack memory base
  //size_t    stacksize; // size of stackp in bytes
  //uintptr_t stackguard0; // [unused]

  T*               parent;    // task that spawned this task
  T*               schedlink; // next task to be scheduled
  _Atomic(TStatus) atomicstatus;
  u64              waitsince; // approx time when the T became blocked
  TFun             fn;        // entry point
} T;

typedef struct M {
  u64        procid;      // process identifier, for debugging (==thrd_current())
  T          t0;          // task with scheduling stack (m_start entry)
  T*         curt;        // current running task
  i64        id;          // identifier from S.mnext (via m_reserve_id)
  u32        locks;       // number of locks held to this M
  T*         lockedt;     // task locked to this M
  P*         p;           // attached p for executing Ts (null if not executing)
  P*         nextp;
  T*         deadq;       // dead tasks waiting to be reclaimed (TDead)
  bool       spinning;    // m is out of work and is actively looking for work
  bool       blocked;     // m is blocked on a note
  TUnlockFun waitunlockf;
  intptr_t   waitunlockv;
  M*         schedlink;
  u32        fastrand[2];
  Note       park;
  bool       doespark; // non-P running threads: sysmon and newmHandoff never use .park
  SigSet     sigmask;  // storage for saved signal mask

  // mstartfn, if set, runs in m_start1 on the OS thread stack
  void(*mstartfn)(void);

  // related to S.freem
  M*  freelink;
  u32 freewait; // if == 0, safe to free t0 and delete M (atomic)

  // os: Platform-specific fields (mOS in Go)
  struct {
    // for C standard <threads.h>
    bool  initialized;
    mtx_t mutex;
    cnd_t cond;
    int   count;
  } os;
} M;

struct P {
  u32      schedtick; // incremented on every scheduler call
  u32      id;        // corresponds to offset in S.allp
  PStatus  status;
  M*       m;         // back-link to associated m (nil if idle)
  P*       link;

  // Queue of runnable tasks. Accessed without lock.
  atomic_u32 runqhead;
  atomic_u32 runqtail;
  T*         runq[P_RUNQSIZE];
  // runnext, if non-nil, is a runnable T that was ready'd by
  // the current T and should be run next instead of what's in
  // runq if there's time remaining in the running T's time
  // slice. It will inherit the time left in the current time
  // slice. If a set of coroutines is locked in a
  // communicate-and-wait pattern, this schedules that set as a
  // unit and eliminates the (potentially large) scheduling
  // latency that otherwise arises from adding the ready'd
  // coroutines to the end of the run queue.
  _Atomic(T*) runnext;

  // Ts – local cache of dead T's
  TList tfree;
  u32   tfreecount;

  Note park;

  // preempt is set to indicate that this P should be enter the
  // scheduler ASAP (regardless of what G is running on it).
  bool preempt;

  // timers
  atomic_u32 numTimers; // Number of timers in P's heap
  // timersLock is the lock for timers. We normally access the timers while running
  // on this P, but the scheduler can also do it from a different P.
  mtx_t timersLock;

  StackFreelist stackcache[3]; // 0=STACK_SIZE_DEFAULT
};

struct S {
  atomic_u64 tidgen;   // next T.ident
  atomic_u64 lastpoll; // time of last poll, or 0 if never polled

  mtx_t lock; // protects access to runq et al

  // Ms
  M*  midle;  // idle m's waiting for work
  u32 midlecount;   // number of idle m's waiting for work
  u32 nmidlelocked; // number of locked m's waiting for work
  i64 mnext;        // number of m's that have been created and next M ID
  u32 maxmcount;    // maximum number of m's allowed (or die)
  i64 nmfreed;      // cumulative number of freed m's

  // freem is the list of m's waiting to be freed when their
  // M.exited is set. Linked through M.freelink.
  M* freem;

  // Ps
  P*          allp[COMAXPROCS_MAX]; // all live P's (managed by s_procresize)
  mtx_t       allplock;
  atomic_u32  maxprocs;   // max active Ps (also num valid P's in allp)
  P*          pidle;      // idle p's
  atomic_u32  npidle;     // number of idling P's at pidle
  atomic_i32  nmspinning; // See "Worker thread parking/unparking" in docs

  // tfree is the global cache of dead T's
  struct {
    mtx_t lock;
    TList stack;   // Ts with stacks
    TList noStack; // Ts without stacks
    u32   n;       // total count of Ts in stack & noStack
  } tfree;

  // runnable queue
  TQueue runq;
  u32    runqsize; // number of T's in runq

  // TODO: T freelist
};

ASSUME_NONNULL_END
