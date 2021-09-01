#include <rbase/rbase.h>
#include "sched.h"
#include "schedimpl.h"
#include "exectx/exectx.h"
#include <pthread.h>

_Pragma("GCC diagnostic ignored \"-Wunused-function\"")

// SCHED_TRACE: when defined, verbose log tracing on stderr is enabled.
// The value is used as a prefix for log messages.
#define SCHED_TRACE "♻ "

// Array is a generic mutable array
typedef struct Array {
  u8* ptr;
  u32 len;
  u32 cap;
  u32 elemsize; // length of one item in the array
} Array;

// VarBitmap is a bitmap of variable size
typedef struct VarBitmap {
  _Atomic(size_t) * ptr;
  u32     len; // number of bits at ptr
} VarBitmap;

// RandomOrder & RandomEnum are helper types for randomized work stealing.
// They allow to enumerate all Ps in different pseudo-random orders without repetitions.
// The algorithm is based on the fact that if we have X such that X and COMAXPROCS
// are coprime, then a sequences of (i + X) % COMAXPROCS gives the required enumeration.
typedef struct RandomOrder {
  u32   count;
  Array coprimes;
} RandomOrder;

typedef struct RandomEnum {
  u32 i;
  u32 count;
  u32 pos;
  u32 inc;
} RandomEnum;

#define PTR_SIZE sizeof(void*)

// global state
static struct S     S = {0};                // global scheduler
static M            m0;                     // main OS thread
static T*           t1;                     // main task on main thread
static bool         mainStarted = false;    // indicates that the main M (m0) has started
static thread_local T* _tlt = NULL;         // current task on current OS thread
static uintptr_t    fastrandseed;           // initialized by fastrandinit
static uintptr_t    hashkey[4] = {1,2,3,4}; // initialized by fastrandinit
static SigSet       initSigmask;            // signal mask for newly created M's
static RandomOrder  stealOrder; // steal order of P's in allp

// execLock serializes exec and clone to avoid bugs or unspecified behaviour
// around exec'ing while creating/destroying threads.  See issue #19546.
static rwmtx_t execLock;

// idlepMask is a bitmask of Ps in PIdle list, one bit per P.
// Reads and writes must be atomic. Length may change at safe points.
//
// Each P must update only its own bit. In order to maintain
// consistency, a P going idle must the idle mask simultaneously with
// updates to the idle P list under the sched.lock, otherwise a racing
// pidleget may clear the mask before s_pidleput sets the mask,
// corrupting the bitmap.
//
// N.B., procresize takes ownership of all Ps in stopTheWorldWithSema.
static VarBitmap idlepMask;

// timerpMask is a bitmask of Ps that may have a timer, one bit per P.
// Reads and writes must be atomic. Length may change at safe points.
static VarBitmap timerpMask;

// allt holds all live T's
static struct {
  mtx_t        lock;
  _Atomic(T**) ptr; // atomic for reading; lock used for writing
  _Atomic(u32) len; // atomic for reading; lock used for writing
  u32          cap; // capacity of ptr array
} allt;

static T* t_get();

static M* m_acquire();
static void m_release(M* m);
static void NORETURN m_call(T* _t_, void(*fn)(T*));
static void NORETURN m_exit(bool osStack);
static void NORETURN schedule();
static void p_runqput(P* p, T* t, bool next);
static void p_tfree_put(P* _p_, T* t);
static void* t_switch(T* t);
static P* nullable s_pidleget();
static void s_pidleput(P* p);
static M* s_midleget();
static void s_midleput(M* m);
static i64 s_reserve_mid();
static void s_newm(P* _p_, void(*fn)(void), i64 id);
static bool p_runqempty(P* p);
static void s_checkdeadlock();
static void m_park();
static void m_semawakeup(M* mp);


// trace(const char* fmt, ...) -- debug tracing
#ifdef SCHED_TRACE
  static void _trace(const char* fmt, ...) {
    T* _t_ = t_get();
    FILE* fp = stderr;
    flockfile(fp);
    const char* prefix = SCHED_TRACE;
    fwrite(prefix, strlen(prefix), 1, fp);
    if (_t_ != NULL) {
      if (_t_->m != NULL) {
        int M_color = 6 - (int)(_t_->m->id % 6); // 3[6-1]
        if (_t_->m->p != NULL) {
          int P_color = 6 - (int)(_t_->m->p->id % 6); // 3[6–1]
          fprintf(fp, "\e[1;4%dmM%lld\e[0m \e[1;4%dmP%u\e[0m T%-2llu ",
            M_color, _t_->m->id,
            P_color, _t_->m->p->id,
            _t_->id);
        } else {
          fprintf(fp, "\e[1;4%dmM%lld\e[0m P- T%-2llu ", M_color, _t_->m->id, _t_->id);
        }
      } else {
        fprintf(fp, "M- T% 2lld] ", (i64)_t_->id);
      }
    } else {
      fprintf(fp, "M- T- ] ");
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    funlockfile(fp);
  }
  #define trace(fmt, ...) \
    _trace("\e[1;36m%-15s\e[39m " fmt "\e[0m\n", __FUNCTION__, ##__VA_ARGS__)
#else
  #define trace(...) do{}while(0)
#endif


static void vbm_resize(VarBitmap* bm, u32 nbits) {
  if (nbits > bm->len) {
    bm->len = nbits;
    bm->ptr = (_Atomic(size_t)*)memrealloc(MemLibC(), bm->ptr, bm->len);
  }
}

static void vbm_free(VarBitmap* bm) {
  if (bm->ptr) {
    memfree(MemLibC(), bm->ptr);
    bm->ptr = NULL;
  }
}

// vbm_read returns true if bit is set
static bool vbm_read(VarBitmap* bm, u32 bit) {
  u32 word = bit / sizeof(size_t);
  u32 mask = ((u32)1) << (bit % sizeof(size_t));
  return (AtomicLoad(&bm->ptr[word]) & mask) != 0;
}

// vbm_set sets bit (to 1)
static void vbm_set(VarBitmap* bm, i32 bit) {
  u32 word = bit / sizeof(size_t);
  u32 mask = ((u32)1) << (bit % sizeof(size_t));
  AtomicOr(&bm->ptr[word], mask);
}

// vbm_clear clears bit (sets it to 0)
static void vbm_clear(VarBitmap* bm, i32 bit) {
  u32 word = bit / sizeof(size_t);
  u32 mask = ((u32)1) << (bit % sizeof(size_t));
  AtomicAnd(&bm->ptr[word], ~mask);
}



// array_grow increases a->cap by nelem
static void array_grow(Array* a, u32 nelem) {
  a->cap += nelem;
  a->ptr = (u8*)memrealloc(MemLibC(), a->ptr, a->cap * a->elemsize);
}

static void array_free(Array* a) {
  if (a->ptr) {
    memfree(MemLibC(), a->ptr);
    a->ptr = NULL;
  }
}

static inline u32 array_len(const Array* a) { return a->len; }
static inline u32 array_cap(const Array* a) { return a->cap; }

static inline uintptr_t array_get(const Array* a, u32 index) {
  return a->ptr[a->elemsize * index];
}

static inline void array_set(const Array* a, u32 index, uintptr_t val) {
  a->ptr[a->elemsize * index] = val;
}

// array_append adds an element to the end of a
static inline void array_append(Array* a, uintptr_t v) {
  if (a->cap == a->len)
    array_grow(a, a->cap == 0 ? 16 : a->cap);
  a->ptr[a->len++] = v;
}

// array_reserve ensures that a has room for nelem elements
static void array_reserve(Array* a, u32 nelem) {
  u32 avail = a->cap - a->len;
  if (avail < nelem)
    array_grow(a, nelem - avail);
}


static const char* TStatusName(TStatus s) {
  switch (s) {
    case TIdle:     return "TIdle";
    case TRunnable: return "TRunnable";
    case TRunning:  return "TRunning";
    case TSyscall:  return "TSyscall";
    case TWaiting:  return "TWaiting";
    case TDead:     return "TDead";
  }
  return "TStatus?";
}


// ===============================================================================================
// Note

#define NOTE_LOCKED  ((uintptr_t)(-1))

// note_clear resets a note
static void note_clear(Note* n) {
  n->key = 0;
}

// note_sleep waits for notification, potentially putting M to sleep until note_wake is called
// for the same note.
static void note_sleep(Note* n) {
  T* t = t_get();
  assert(t == &t->m->t0 /* must only wait on a note in M scheduling context */);

  TODO_IMPL;

  // uintptr_t expect = 0;
  // if (!AtomicCasRelAcq(&n.key, &expect, (uintptr_t)&m)) {
  //   // Must be locked (got sema_wake).
  //   if (expect != kLocked) {
  //     panic("m_wait out of sync");
  //   }
  //   return;
  // }
}

// note_wakeup notifies callers to note_sleep
static void note_wakeup(Note* n) {
  uintptr_t v = AtomicLoad(&n->key);
  while (!AtomicCAS(&n->key, &v, NOTE_LOCKED)) {
    // note that AtomicCAS loads current val of n.key on failure
  }
  switch (v) {
  case 0:
    // Nothing was waiting. Done.
    break;
  case NOTE_LOCKED:
    // Two note_wakeup! Not allowed.
    panic("double note_wakeup");
    break;
  default:
    // Must be the waiting M. Wake it up
    m_semawakeup((M*)v);
    break;
  }
}

// ===============================================================================================
// TQueue and TList

// TQueueEmpty reports whether q is empty
static inline bool TQueueEmpty(const TQueue* q) {
  return q->head == NULL;
}

// TQueuePush adds t to the head of q
static void TQueuePush(TQueue* q, T* t) {
  t->schedlink = q->head;
  q->head = t;
  if (!q->tail)
    q->tail = t;
}

// TQueuePushBack adds t to the tail of q
static void TQueuePushBack(TQueue* q, T* t) {
  t->schedlink = NULL;
  if (q->tail) {
    q->tail->schedlink = t;
  } else {
    q->head = t;
  }
  q->tail = t;
}

// TQueuePushBackAll adds all Ts in q2 to the tail of q.
// After this q2 must not be used.
static void TQueuePushBackAll(TQueue* q, TQueue* q2) {
  if (!q2->tail)
    return;
  q2->tail->schedlink = NULL;
  if (q->tail) {
    q->tail->schedlink = q2->head;
  } else {
    q->head = q2->head;
  }
  q->tail = q2->tail;
}

// TQueuePop removes and returns the head of queue q.
// Returns NULL if q is empty.
static T* nullable TQueuePop(TQueue* q) {
  T* t = q->head;
  if (t) {
    q->head = t->schedlink;
    if (!q->head)
      q->tail = NULL;
  }
  return t;
}

// TQueuePopList takes all Ts in q and returns them as a TList
static TList TQueuePopList(TQueue* q) {
  TList stack = { q->head };
  q->head = NULL;
  q->tail = NULL;
  return stack;
}


// TListEmpty reports whether l is empty
static inline bool TListEmpty(const TList* l) {
  return l->head == NULL;
}

// TListPush adds t to the head of l
static void TListPush(TList* l, T* t) {
  t->schedlink = l->head;
  l->head = t;
}

// TListPushAll prepends all Ts in q to l
static void TListPushAll(TList* l, TQueue* q) {
  if (!TQueueEmpty(q)) {
    q->tail->schedlink = l->head;
    l->head = q->head;
  }
}

// TListPop removes and returns the head of l. If l is empty, it returns NULL.
static T* nullable TListPop(TList* l) {
  T* t = l->head;
  if (t)
    l->head = t->schedlink;
  return t;
}


// ===============================================================================================
// RandomOrder


static u32 gcd(u32 a, u32 b) {
  while (b != 0) {
    // a, b = b, a%b
    u32 tmp = a % b;
    a = b;
    b = tmp;
  }
  return a;
}

static void randord_init(RandomOrder* ord) {
  ord->coprimes.elemsize = sizeof(u32);
}

static void randord_reset(RandomOrder* ord, u32 count) {
  ord->count = count;
  ord->coprimes.len = 0;
  array_reserve(&ord->coprimes, count);
  for (u32 i = 1; i <= count; i++) {
    if (gcd(i, count) == 1)
      array_append(&ord->coprimes, i);
  }
}

static RandomEnum randord_start(RandomOrder* ord, u32 i) {
  return (RandomEnum){
    .count = ord->count,
    .pos   = i % ord->count,
    .inc   = array_get(&ord->coprimes, i % array_len(&ord->coprimes)),
  };
}

static inline bool randenum_done(const RandomEnum* e) {
  return e->i == e->count;
}

static inline void randenum_next(RandomEnum* e) {
  e->i++;
  e->pos = (e->pos + e->inc) % e->count;
}

static inline u32 randenum_pos(const RandomEnum* e) {
  return e->pos;
}


// ===============================================================================================
// misc crypto

static int rand_read(void* buf, size_t size) {
  int fd = open("/dev/urandom", O_RDONLY, 0);
  int n = read(fd, buf, size);
  close(fd);
  return n;
}

static u32 load_unaligned32(uintptr_t p) {
  u8* q = (u8*)&p;
  #if R_TARGET_ARCH_LE
    return (u32)(q[0]) | (u32)(q[1])<<8 | (u32)(q[2])<<16 | (u32)(q[3])<<24;
  #else
    return (u32)(q[3]) | (u32)(q[2])<<8 | (u32)(q[1])<<16 | (u32)(q[0])<<24;
  #endif
}

// memhash hashes a pointer-sized integer
// From go runtime/hash64.go and runtime/alg.go
static uintptr_t memhash(uintptr_t p, uintptr_t seed);
#if R_TARGET_ARCH_SIZE > 32
  static inline u64 rotl_31(u64 x) {
    return (x << 31) | (x >> (64 - 31));
  }
  static uintptr_t memhash(uintptr_t p, uintptr_t seed) {
    // Constants for multiplication: four random odd 64-bit numbers.
    const u64 m1 = 16877499708836156737ull;
    const u64 m2 = 2820277070424839065ull;
    const u64 m3 = 9497967016996688599ull;
    //const u64 m4 = 15839092249703872147ull;
    u64 h = (u64)(seed + 8*hashkey[0]);
    h ^= (u64)(load_unaligned32(p)) | (u64)(load_unaligned32(p + 4)) << 32;
    h = rotl_31(h * m1) * m2;
    h ^= h >> 29;
    h *= m3;
    h ^= h >> 32;
    return (uintptr_t)h;
  }
#else
  #error "TODO: memhash not implemented for 32-bit arch"
#endif

static void fastrandinit() {
  uintptr_t r[countof(hashkey) + 1];
  rand_read((void*)&r, sizeof(r));
  fastrandseed = r[0];
  hashkey[0] = r[1] | 1; // make sure these numbers are odd
  hashkey[1] = r[2] | 1;
  hashkey[2] = r[3] | 1;
  hashkey[3] = r[4] | 1;
}

static u32 m_fastrand(M* _m_);

static u32 fastrand() {
  return m_fastrand(t_get()->m);
}


// m_semacreate creates a semaphore for mp, if it does not already have one.
static void m_semacreate(M* mp) {
  if (mp->os.initialized)
    return;
  mp->os.initialized = true;
  if (mtx_init(&mp->os.mutex, mtx_plain) != 0)
    panic("mtx_init");
  if (cnd_init(&mp->os.cond) != 0)
    panic("cnd_init");
}

// m_semasleep waits for a m_semawakeup call with optional timeout (ns<0 means no timeout)
// If ns < 0, acquire M's semaphore and return 0.
// If ns >= 0, try to acquire M's semaphore for at most ns nanoseconds.
// Return true if the semaphore was acquired, false if interrupted or timed out.
static bool m_semasleep(i64 ns) {
  u64 start = 0;
  if (ns >= 0)
    start = nanotime();
  M* mp = t_get()->m;
  bool success = false;
  mtx_lock(&mp->os.mutex);
  while (1) {
    if (mp->os.count > 0) {
      mp->os.count--;
      success = true;
      break;
    }
    if (ns >= 0) {
      i64 spent = (i64)(nanotime() - start);
      if (spent >= ns) // timeout
        break;
      i64 ns2 = ns - spent;
      struct timespec deadline;
      if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        break;
      deadline.tv_sec += (time_t)(ns2 / 1000000000);
      deadline.tv_nsec += (long)(ns2 - ((ns2 / 1000000000) * 1000000000));
      if (cnd_timedwait(&mp->os.cond, &mp->os.mutex, &deadline) == thrd_timedout)
        break;
    } else {
      cnd_wait(&mp->os.cond, &mp->os.mutex);
    }
  }
  mtx_unlock(&mp->os.mutex);
  return success;
}

// m_semawakeup wakes up mp, which is or will soon be sleeping on its semaphore.
static void m_semawakeup(M* mp) {
  mtx_lock(&mp->os.mutex);
  mp->os.count++;
  if (mp->os.count > 0)
    cnd_signal(&mp->os.cond);
  mtx_unlock(&mp->os.mutex);
}


// ===============================================================================================
// stack


// stackalloc allocates stack memory of approximately reqsize size (aligned to memory page size.)
// Returns the low address (top of stack; not the SB), the actual size in stacksize_out
// and guard size in guardsize_out (stacksize_out - guardsize_out = usable stack space.)
//
// On platforms that support it, stack memory is allocated "lazily" so that only when a page
// is used is it actually committed & allocated in actual memory. On POSIX systems mmap is used
// and on MS Windows VirtualAlloc is used (the latter is currently not implemented.)
//
// [implemented in stack_*.c]
u8* stackalloc(size_t reqsize, size_t* size_outsize_t, size_t* guardsize_out);

// stackfree free stack memory at lo (low stack address) of size.
bool stackfree(void* lo, size_t size); // implemented in stack_*.c


// ===============================================================================================
// T


// t_get returns the current task
static ALWAYS_INLINE T* t_get() {
  // Force inline to allow call-site TLS load optimizations.
  // Example:
  //   void foo() {
  //     int s = 0;
  //     for (size_t i = 10; i != 0; ++i) {
  //       s += t_get().x;
  //     }
  //   }
  // If t_get would not be inline, we would perform 10 TLS loads here, but
  // making t_get inline, the compiler optimizes the TLS load by moving it
  // outside of the loop, effectively:
  //   void foo() {
  //     int s = 0;
  //     auto& tmp = t_get();
  //     for (size_t i = 10; i != 0; ++i) {
  //       s += tmp.x;
  //     }
  //   }
  return _tlt;
}

// t_stacksize returns T's stack size
static inline size_t t_stacksize(T* t) {
  return (size_t)(t->stack.hi - t->stack.lo);
}

// t_readstatus returns the value of t->atomicstatus using a relaxed atomic memory operation.
// On many archs, including x86, this is just a plain load.
static TStatus t_readstatus(T* t) {
  return AtomicLoad(&t->atomicstatus);
}

// t_setstatus updates t->atomicstatus using using an atomic store.
// This is only used when setting up new or recycled Ts.
// To change T.status for an existing T, use t_casstatus instead.
static void t_setstatus(T* t, TStatus newval) {
  AtomicStore(&t->atomicstatus, newval);
}

// t_casstatus updates t->atomicstatus using compare-and-swap.
// It blocks until it succeeds (until t->atomicstatus==oldval.)
static void t_casstatus(T* t, TStatus oldval, TStatus newval) {
  assert(oldval != newval);

  // use a temporary variable for oldval since AtomicCAS will store the current value
  // to it on failure, but we want to swap values only when the current value is
  // explicitly oldval.
  TStatus oldvaltmp = oldval;

  // // See https://golang.org/cl/21503 for justification of the yield delay.
  // const u64 yieldDelay = 5 * 1000;
  // u64 nextYield = 0;

  for (int i = 0; !AtomicCAS(&t->atomicstatus, &oldvaltmp, newval); i++) {
    // Note: on failure, when we get here, oldval has been updated; we compare it
    assert(!(oldval == TWaiting && oldvaltmp == TRunnable)
           /* waiting for TWaiting but is TRunnable */);
    oldvaltmp = oldval; // restore oldvaltmp for next attempt

    // TODO:
    // if (i == 0)
    //   nextYield = nanotime() + yieldDelay;
    //
    // if (nanotime() < nextYield) {
    //   for x := 0; x < 10 && t.atomicstatus != oldval; x++ {
    //     procyield(1);
    //   }
    // } else {
    //   osyield();
    //   nextYield = nanotime() + yieldDelay/2;
    // }
    // TODO: temporary solution until the above has been researched & impl:
    usleep(3); // sleep 3ns
  }
}

// m_dropt removes the association between m and the current coroutine m->curt (T for short).
// Typically a caller sets T's status away from TRunning and then immediately calls m_dropt
// to finish the job. The caller is also responsible for arranging that T will be restarted
// using ready at an appropriate time. After calling m_dropt and arranging for T to be
// readied later, the caller can do other work but eventually should call schedule to restart
// the scheduling of coroutines on this m.
static void m_dropt() {
  T* _t_ = t_get();
  trace("");
  assert(_t_->m->curt != _t_ /* current T should be different than m->curt */);
  _t_->m->curt->m = NULL;
  _t_->m->curt = NULL;
}

// t_exit is called on M.t0's stack after t_main exits; to end t.
static void NORETURN t_exit(T* t) {
  T* _t_ = t_get();
  trace("T#%llu", t->id);
  assert(_t_ == &_t_->m->t0);
  assert(_t_ != t);

  t_casstatus(t, TRunning, TDead);

  bool locked = t->lockedm != NULL;
  t->m = NULL;
  t->lockedm = NULL;
  t->parent = NULL;
  // t->fn = NULL;

  m_dropt();

  // put T on tfree or free it (unless user-allocated)
  if ((t->fl & TFlUserStack) == 0)
    p_tfree_put(_t_->m->p, t);

  if (locked) {
    trace("lockedm");
    // The coroutine may have locked this thread because it put it in an unusual kernel state.
    // Kill it rather than returning it to the thread pool.

    // Return to m_start, which will release the P and exit the thread.
    TODO_IMPL; //gogo(&_t_->m->t0);
  }

  schedule();
  UNREACHABLE;
}

// _t_exit0 finishes execution of the current coroutine.
// It is called when the coroutine's function returns.
// This function is link exported because it's called from assembly.
void NORETURN _t_exit0() {
  trace("");
  m_call(t_get(), t_exit); // never returns
}

static void NORETURN exitprog(int status) {
  trace("\e[1;35m" "PROGRAM EXIT");
  trace("TODO: close() child tasks");
  // TODO: consider returning from sched_main instead of exit()ing
  exit(status);
}


// // Mark gp ready to run.
// func ready(gp *g, traceskip int, next bool) {
//   if trace.enabled {
//     traceGoUnpark(gp, traceskip)
//   }

//   status := readgstatus(gp)

//   // Mark runnable.
//   _g_ := getg()
//   mp := acquirem() // disable preemption because it can be holding p in a local var
//   if status&^_Gscan != _Gwaiting {
//     dumpgstatus(gp)
//     throw("bad g->status in ready")
//   }

//   // status is Gwaiting or Gscanwaiting, make Grunnable and put on runq
//   casgstatus(gp, _Gwaiting, _Grunnable)
//   runqput(_g_.m.p.ptr(), gp, next)
//   wakep()
//   releasem(mp)
// }

// // Puts the current goroutine into a waiting state and calls unlockf on the
// // system stack.
// //
// // If unlockf returns false, the goroutine is resumed.
// //
// // unlockf must not access this G's stack, as it may be moved between
// // the call to gopark and the call to unlockf.
// //
// // Note that because unlockf is called after putting the G into a waiting
// // state, the G may have already been readied by the time unlockf is called
// // unless there is external synchronization preventing the G from being
// // readied. If unlockf returns false, it must guarantee that the G cannot be
// // externally readied.
// //
// // Reason explains why the goroutine has been parked. It is displayed in stack
// // traces and heap dumps. Reasons should be unique and descriptive. Do not
// // re-use reasons, add new ones.
// func gopark(unlockf func(*g, unsafe.Pointer) bool, lock unsafe.Pointer, reason waitReason, traceEv byte, traceskip int) {
//   if reason != waitReasonSleep {
//     checkTimeouts() // timeouts may expire while two goroutines keep the scheduler busy
//   }
//   mp := acquirem()
//   gp := mp.curg
//   status := readgstatus(gp)
//   if status != _Grunning && status != _Gscanrunning {
//     throw("gopark: bad g status")
//   }
//   mp.waitlock = lock
//   mp.waitunlockf = unlockf
//   gp.waitreason = reason
//   mp.waittraceev = traceEv
//   mp.waittraceskip = traceskip
//   releasem(mp)
//   // can't do anything that might move the G between Ms here.
//   mcall(park_m)
// }

// // sched_sched yields the processor, allowing other coroutines to run.
// // It does not suspend the current coroutine, so execution resumes automatically.
// func sched_sched() {
//   // checkTimeouts()
//   m_call(gosched_m)
// }

static void t_yield1(T* t) {
  trace("T#%llu", t->id);
  P* p = t->m->p;
  t_casstatus(t, TRunning, TRunnable);
  m_dropt();
  p_runqput(p, t, /*next*/false);
  schedule();
}

// t_yield puts the current T on the runq of the current P
// (instead of the globrunq, as sched_sched does)
void t_yield() {
  // checkTimeouts()
  T* _t_ = t_get();
  trace("exectx_save");
  if (exectx_save(_t_->exectx) == 0)
    m_call(_t_, t_yield1);
  trace("resumed");
  // resumed
}

// // t_switch switches execution from _t_ to t.
// // It sets _t_ to t before switching and restores _t_ when t returns.
// // Returns the value passed to the yielding exectx_switch.
// static void* t_switch(T* t) {
//   T* _t_ = t_get();

//   // must never t_switch to t0 (use exectx_jump instead).
//   // t0's stack context should not advance.
//   assert(t != &t->m->t0);

//   trace("M#%lld : depart T#%llu -> T#%llu (t0 SP %zu)",
//     _t_->m->id, _t_->id, t->id, _t_->m->t0.stack.hi - (uintptr_t)&_t_);

//   _tlt = t;
//   exectx_t c = exectx_switch(t->stackctx, t);
//   t->stackctx = c.ctx;
//   _tlt = _t_;
//   trace("M#%lld : return T#%llu <- T#%llu", _t_->m->id, _t_->id, t->id);
//   return c.data;
// }

// t_execute schedules t to run on the current M.
// If inheritTime is true, t inherits the remaining time in the current time slice.
// Otherwise, it starts a new time slice.
// Never returns.
static void NORETURN t_execute(T* t, bool inheritTime) {
  T* _t_ = t_get();
  assert(_t_ == &_t_->m->t0);
  trace("T#%llu on M#%lld", t->id, _t_->m->id);

  // Assign t->m before entering TRunning so running Ts have an M
  _t_->m->curt = t;
  t->m = _t_->m;
  t_casstatus(t, TRunnable, TRunning);
  t->waitsince = 0;
  t->parent = _t_;
  // t->preempt = false;
  // t->stackguard0 = t->stack.lo + STACK_GUARD;
  if (!inheritTime) {
    _t_->m->p->schedtick++;
  }

  // gogo switches stacks in Go. It's implemented in runtime/asm_ARCH.s
  // gogo(&t->sched)

  trace("exectx_resume");
  _tlt = t;
  exectx_resume(t->exectx, (uintptr_t)t);

  // // t_switch switches stacks; restores t's register state and resumes execution of t->fn
  // auto fn = (void(*)(T*)) t_switch(t);

  // assert(t_get() == _t_);

  // // continuation from m_call
  // trace("m_call from T#%llu; now on t0; calling m_call's fn", t->id);
  // fn(t); // never returns

  UNREACHABLE;
}

// t_init initializes a T at the bottom of stack memory starting at low address lo.
// Note that the returned pointer will be at lo[stacksize - aligned16_sizeof(T)], not at lo.
static T* t_init(u8* lo, size_t stacksize) {
  T* newt = (T*)&lo[stacksize - STACK_TSIZE];

  // initialize T
  memset(newt, 0, sizeof(T));
  newt->atomicstatus = TDead;
  newt->stack.lo = (uintptr_t)lo;
  newt->stack.hi = (uintptr_t)&lo[stacksize];

  trace("T: %p, stack: [lo=%p - hi=%p] (%zu B, %g pages)",
    newt, lo, &lo[stacksize], stacksize, (double)stacksize / mem_pagesize());

  // // Go records T there on gsignal stack during VDSO on ARM and ARM64, but we store
  // // T at the stack top (except for M->t0).
  // *((uintptr_t*)&lo[stacksize - tsize - sizeof(void*)]) = 0;

  // initialize stack data for context switching and save a pointer to it
  // newt->stackctx = exectx_init(&lo[stacksize - STACK_TSIZE], stacksize - STACK_TSIZE, t_main);

  return newt;
}

// t_alloc allocates a new T, with a stack big enough for requested_stacksize bytes.
// If requested_stacksize == 0, an sufficiently large, implementation-varying stack size is used.
static T* t_alloc(size_t requested_stacksize) {
  // stack layout
  //
  // 0x0000  end of stack (T.hi)
  //   guard page (1 page long; only if enabled in stack.c)
  // 0x1000  end of program stack
  //   ...
  //   program data
  //   ...
  // 0x1FD0  beginning of program stack
  //   T storage
  // 0x2000  beginning of stack (T.hi)
  //

  // Make sure stack fits T and is at least STACK_MIN
  if (requested_stacksize == 0) {
    requested_stacksize = STACK_SIZE_DEFAULT;
  } else if (requested_stacksize < STACK_MIN + sizeof(T)) {
    requested_stacksize = STACK_MIN + sizeof(T);
  }

  // allocate memory
  size_t stacksize = 0;
  size_t guardsize = 0;
  u8* lo = (u8*)stackalloc(requested_stacksize, &stacksize, &guardsize);
  if (lo == NULL)
    return NULL; // most likely out of memory (see errno)

  return t_init(lo, stacksize);
}

// t_free frees memory of T, including its stack where it's allocated
static void t_free(T* t) {
  assert((t->fl & TFlUserStack) == 0 /* must not free T with user-provided stack */);
  trace("T#%llu: [%p - %p], stack: [lo=%zx - hi=%zx] (%zu)",
    t->id, t, &((u8*)t)[sizeof(*t)], t->stack.lo, t->stack.hi, t_stacksize(t));
  // memset(t, 0, sizeof(*t));
  stackfree((void*)t->stack.lo, t_stacksize(t));
}



// ===============================================================================================
// allt


// allt_add adds t to allt
static void allt_add(T* t) {
  if (t_readstatus(t) == TIdle)
    panic("allt_add: bad status TIdle");

  mtx_lock(&allt.lock);
  if (allt.len == allt.cap) {
    trace("grow array");
    allt.cap = allt.cap + 64;
    allt.ptr = memrealloc(MemLibC(), allt.ptr, allt.cap);
    AtomicStore(&allt.ptr, allt.ptr);
  }
  trace("add");
  allt.ptr[allt.len++] = t;
  AtomicStore(&allt.len, allt.len);
  mtx_unlock(&allt.lock);
}


// ===============================================================================================
// P


// p_tfree_get gets a T from tfree list. Returns NULL if no free T was available.
static T* nullable p_tfree_get(P* _p_) {
  while (1) { // loop for retrying

    if (TListEmpty(&_p_->tfree) && !TListEmpty(&S.tfree.l)) {
      mtx_lock(&S.tfree.lock);
      // Move a batch of free Gs to the P.
      while (_p_->tfreecount < 32) {
        // Prefer Gs with stacks
        T* t = TListPop(&S.tfree.l);
        if (t == NULL)
          break;
        S.tfree.n--;
        TListPush(&_p_->tfree, t);
        _p_->tfreecount++;
      }
      mtx_unlock(&S.tfree.lock);
      continue; // retry
    }

    T* t = TListPop(&_p_->tfree);
    if (t == NULL)
      return NULL;

    _p_->tfreecount--;

    // Note: Go implementation allocates a task separately from its stack.
    // However we use the bottom of a task's stack for the task state (T) itself, so tasks
    // on the free list always has an associated stack.
    // The below code is kept here for documentation and reference:
    // if (t->stack.lo == 0) {
    //   // Stack was deallocated in p_tfree_put. Allocate a new one.
    //   t->stack = stack_alloc(FIXED_STACK);
    //   t->stackguard0 = t->stack.lo + STACK_GUARD;
    // }

    //else {
    //  if (raceenabled)
    //    racemalloc(unsafe.Pointer(t->stack.lo), t->stack.hi - t->stack.lo)
    //  if (msanenabled)
    //    msanmalloc(unsafe.Pointer(t->stack.lo), t->stack.hi - t->stack.lo)
    //}

    return t;
  }
}

// p_tfree_put reclaims dead T, to be reused for new tasks. Puts on tfree list.
// If local list is too long, transfer a batch to the global list.
static void p_tfree_put(P* _p_, T* t) {
  assert(t_readstatus(t) == TDead);
  assert((t->fl & TFlUserStack) == 0 /* must not be T with user-provided stack */);

  uintptr_t stacksize = t_stacksize(t);

  // See note about Go's implementation of separate storage for T and its stack in p_tfree_get
  //if (stacksize != FIXED_STACK) {
  //  // non-standard stack size - free it.
  //  stack_free(t->stack);
  //  t->stack.lo = 0;
  //  t->stack.hi = 0;
  //  t->stackguard0 = 0;
  //}
  if (stacksize != STACK_SIZE_DEFAULT) {
    // don't keep tasks with non-default stack sizes
    trace("non-standard stack size (%zu != %zu)", stacksize, (size_t)STACK_SIZE_DEFAULT);
    t_free(t);
    return;
  }

  // t->fn = NULL;

  TListPush(&_p_->tfree, t);
  _p_->tfreecount++;

  // If local list is too long, transfer a batch to the global list
  if (_p_->tfreecount >= 64) {
    u32 inc = 0;
    TQueue q = {0};
    while (_p_->tfreecount >= 32) {
      t = TListPop(&_p_->tfree);
      _p_->tfreecount--;
      TQueuePush(&q, t);
      inc++;
    }
    mtx_lock(&S.tfree.lock);
    TListPushAll(&S.tfree.l, &q);
    S.tfree.n += inc;
    mtx_unlock(&S.tfree.lock);
  }
}

// p_tfree_purge purges all cached T's from P's tfree list to the global list S.tfree
static void p_tfree_purge(P* _p_) {
  u32 inc = 0;
  TQueue q = {0};
  while (!TListEmpty(&_p_->tfree)) {
    T* t = TListPop(&_p_->tfree);
    _p_->tfreecount--;
    TQueuePush(&q, t);
    inc++;
  }
  mtx_lock(&S.tfree.lock);
  TListPushAll(&S.tfree.l, &q);
  S.tfree.n += inc;
  mtx_unlock(&S.tfree.lock);
}

// p_acquire associates P with the current M
static void p_acquire(P* p) {
  M* m = t_get()->m;
  assert(m->p == NULL /* else: M in use by other P */);
  #ifndef NDEBUG
  if (p->m != NULL || p->status != PIdle) {
    errlog("p_acquire: p->m=%p, p->status=%u", p->m, p->status);
    assert(!"invalid P state");
  }
  #endif
  m->p = p;
  p->m = m;
  p->status = PRunning;
}

// p_release disassociates P from the current M
// Returns the disassociated P.
static P* p_release() {
  M* m = t_get()->m;
  P* _p_ = m->p;
  assert(_p_ != NULL);

  if (_p_->m != m || _p_->status != PRunning) {
    errlog("p_release: _p_->m=%p, m=%p ,_p_->status=%u", _p_->m, m, _p_->status);
    assert(_p_->m == m);
    assert(_p_->status == PRunning);
  }

  _p_->m->p = NULL;
  _p_->m = NULL;
  _p_->status = PIdle;
  return _p_;
}



static void p_startm_mspinning(void) {
  trace("");
  // startm's caller incremented nmspinning. Set the new M's spinning.
  t_get()->m->spinning = true;
}


// p_startm schedules some M to run the p (creates an M if necessary).
//
// If p==NULL, tries to get an idle P, if no idle P's does nothing.
// May run with m->p==NULL.
// If spinning is set, the caller has incremented nmspinning and startm will
// either decrement nmspinning or set m->spinning in the newly started M.
//
// Callers passing a non-nil P must call from a non-preemptible context
static void p_startm(P* _p_, bool spinning) { // [go: startm]
  // Disable preemption.
  //
  // Every owned P must have an owner that will eventually stop it in the
  // event of a GC stop request. p_startm takes transient ownership of a P
  // (either from argument or s_pidleget below) and transfers ownership to
  // a started M, which will be responsible for performing the stop.
  //
  // Preemption must be disabled during this transient ownership,
  // otherwise the P this is running on may enter GC stop while still
  // holding the transient P, leaving that P in limbo and deadlocking the
  // STW.
  //
  // Callers passing a non-nil P must already be in non-preemptible
  // context, otherwise such preemption could occur on function entry to
  // startm. Callers passing a nil P may be preemptible, so we must
  // disable preemption before acquiring a P from s_pidleget below.
  M* mp = m_acquire();
  mtx_lock(&S.lock);
  trace("");

  if (!_p_) {
    _p_ = s_pidleget();
    if (!_p_) {
      trace("no idle P's");
      mtx_unlock(&S.lock);
      if (spinning) {
        // The caller incremented nmspinning, but there are no idle Ps,
        // so it's okay to just undo the increment and give up.
        u32 z = AtomicSub(&S.nmspinning, 1) - 1;
        assert(z != 0xFFFFFFFF /* nmspinning decrement does not match increment */);
      }
      m_release(mp);
      return;
    }
  }

  // try to acquire an idle M
  M* nmp = s_midleget();

  if (!nmp) {
    // No M is available, we must drop S.lock and call s_newm.
    // However, we already own a P to assign to the M.
    //
    // Once S.lock is released, another T (e.g., in a syscall),
    // could find no idle P while checkdead finds a runnable T but
    // no running M's because this new M hasn't started yet, thus
    // throwing in an apparent deadlock.
    //
    // Avoid this situation by pre-allocating the ID for the new M,
    // thus marking it as 'running' before we drop S.lock. This
    // new M will eventually run the scheduler to execute any
    // queued T's.
    i64 id = s_reserve_mid();
    mtx_unlock(&S.lock);

    void(*fn)(void) = NULL;
    if (spinning) {
      // The caller incremented nmspinning, so set m.spinning in the new M.
      fn = p_startm_mspinning;
    }
    s_newm(_p_, fn, id);
    // Ownership transfer of _p_ committed by start in s_newm.
    // Preemption is now safe.
    m_release(mp);
    return;
  }

  mtx_unlock(&S.lock);

  assert(!nmp->spinning);
  assert(nmp->nextp == 0 /* M should not have a P */);
  if (spinning && !p_runqempty(_p_)) {
    panic("startm: p has runnable gs");
  }
  // The caller incremented nmspinning, so set m.spinning in the new M.
  nmp->spinning = spinning;
  nmp->nextp = _p_;
  note_wakeup(&nmp->park);
  // Ownership transfer of _p_ committed by wakeup. Preemption is now
  // safe.
  m_release(mp);
}


// p_runqempty returns true if p has no Ts on its local run queue.
// It never returns true spuriously.
static bool p_runqempty(P* p) {
  // return p->runqhead == p->runqtail && p->runnext == 0; //< unlocked impl

  // Defend against a race where
  // 1) p has T1 in runqnext but runqhead == runqtail,
  // 2) p_runqput on p kicks T1 to the runq, 3) runqget on p empties runqnext.
  // Simply observing that runqhead == runqtail and then observing that runqnext == NULL
  // does not mean the queue is empty.
  while (1) {
    u32 head = AtomicLoad(&p->runqhead);
    u32 tail = AtomicLoad(&p->runqtail);
    T* runnext = AtomicLoad(&p->runnext);
    if (tail == AtomicLoad(&p->runqtail)) {
      return head == tail && runnext == 0;
    }
  }
}

// Put t and a batch of work from local runnable queue on global queue.
// Executed only by the owner P.
static bool p_runqputslow(P* p, T* t, u32 head, u32 tail) {
  // TODO
  return false;
}

// p_runqput tries to put t on the local runnable queue.
// If next if false, runqput adds T to the tail of the runnable queue.
// If next is true, runqput puts T in the p.runnext slot.
// If the run queue is full, runnext puts T on the global queue.
// Executed only by the owner P.
static void p_runqput(P* p, T* t, bool next) {
  // if randomizeScheduler && next && fastrand1()%2 == 0 {
  //   next = false
  // }
  T* tp = t;
  if (next) {
    // puts t in the p.runnext slot.
    T* oldnext = p->runnext;
    while (!AtomicCAS(&p->runnext, &oldnext, t)) {
      // Note that when AtomicCAS fails, it performs a loads of the current value into oldnext,
      // thus we can simply loop here without having to explicitly load oldnext=p->runnext.
    }
    if (oldnext == NULL)
      return;
    // Kick the old runnext out to the regular run queue
    tp = oldnext;
  }

  while (1) {
    // load-acquire, sync with consumers
    u32 head = AtomicLoadAcq(&p->runqhead);
    u32 tail = p->runqtail;
    if (tail - head < P_RUNQSIZE) {
      trace("put T#%llu at runq[%u]", tp->id, tail % P_RUNQSIZE);
      p->runq[tail % P_RUNQSIZE] = tp;
      // store memory_order_release makes the item available for consumption
      AtomicStoreRel(&p->runqtail, tail + 1);
      return;
    }
    // Put t and move half of the locally scheduled runnables to global runq
    if (p_runqputslow(p, tp, head, tail)) {
      return;
    }
    // the queue is not full, now the put above must succeed. retry...
  }
}

// Get T from local runnable queue.
// If inheritTime is true, T should inherit the remaining time in the current time slice.
// Otherwise, it should start a new time slice.
// Executed only by the owner P.
static T* p_runqget(P* p, bool* inheritTime) {
  // If there's a runnext, it's the next G to run.
  while (1) {
    T* next = p->runnext;
    if (next == NULL)
      break;
    if (AtomicCAS(&p->runnext, &next, NULL)) {
      *inheritTime = true;
      return next;
    }
  }
  trace("no runnext; trying dequeue p->runq");

  *inheritTime = false;

  while (1) {
    u32 head = AtomicLoadAcq(&p->runqhead); // load-acquire, sync with consumers
    u32 tail = p->runqtail;
    if (tail == head)
      return NULL;
    // trace("loop2 tail != head; load p->runq[%u]", head % P_RUNQSIZE);
    T* tp = p->runq[head % P_RUNQSIZE];
    // trace("loop2 tp => %p", tp);
    // trace("loop2 tp => T#%llu", tp->id);
    if (AtomicCASRel(&p->runqhead, &head, head + 1)) // cas-release, commits consume
      return tp;
    trace("CAS failure; retry");
  }
}

// p_runqgrab grabs a batch of goroutines from _p_'s runnable queue into batch.
// Batch is a ring buffer starting at batchHead.
// Returns number of grabbed goroutines.
// Can be executed by any P.
static u32 p_runqgrab(P* _p_, T* batch[P_RUNQSIZE], u32 batchHead, bool stealRunNextT) {
  trace("P#%u", _p_->id);
  while (1) {
    auto h = AtomicLoadAcq(&_p_->runqhead); // load-acquire, synchronize with other consumers
    auto t = AtomicLoadAcq(&_p_->runqtail); // load-acquire, synchronize with the producer
    auto n = t - h;
    n = n - n/2;
    if (n == 0) {
      if (stealRunNextT) {
        // Try to steal from _p_.runnext.
        T* next = _p_->runnext;
        if (next != 0) {
          if (_p_->status == PRunning) {
            // Sleep to ensure that _p_ isn't about to run the T we are about to steal.
            // The important use case here is when the T running on _p_ ready()s another T
            // and then almost immediately blocks. Instead of stealing runnext in this window,
            // back off to give _p_ a chance to schedule runnext. This will avoid thrashing gs
            // between different Ps. A sync chan send/recv takes ~50ns as of time of writing,
            // so 3us gives ~50x overshoot.
            usleep(3);
            // TODO: on Windows, use osyield() instead of usleep()
          }
          if (!AtomicCAS(&_p_->runnext, &next, NULL))
            continue;
          batch[batchHead % P_RUNQSIZE] = next;
          return 1;
        }
      }
      return 0;
    }
    if (n > (u32)(P_RUNQSIZE / 2)) // read inconsistent h and t
      continue;
    for (u32 i = 0; i < n; i++) {
      T* t = _p_->runq[(h + i) % P_RUNQSIZE];
      batch[(batchHead + i) % P_RUNQSIZE] = t;
    }
    if (AtomicCASRel(&_p_->runqhead, &h, h + n)) // cas-release, commits consume
      return n;
  }
}

// p_runqsteal steals half of elements from local runnable queue of p2
// and put onto local runnable queue of p.
// Returns one of the stolen elements (or nil if failed).
static T* nullable p_runqsteal(P* _p_, P* p2, bool stealRunNextT) {
  u32 tail = _p_->runqtail;
  u32 n = p_runqgrab(p2, _p_->runq, tail, stealRunNextT);
  if (n == 0)
    return NULL;
  n--;
  T* t = _p_->runq[(tail + n) % P_RUNQSIZE];
  if (n == 0)
    return t;
  u32 h = AtomicLoadAcq(&_p_->runqhead); // load-acquire, synchronize with consumers
  if (tail - h + n >= P_RUNQSIZE)
    panic("p_runqsteal: runq overflow");
  AtomicStoreRel(&_p_->runqtail, tail+n); // store-release, makes the item available for consumption
  return t;
}

// p_wake tries to add one more P to execute T's.
// Called when a T is made runnable (sched_spawn, ready).
static void p_wake() {
  u32 npidle = AtomicLoad(&S.npidle);
  trace("npidle=%u", npidle);
  if (npidle == 0) {
    trace("none (S.npidle==0)");
    return;
  }

  // be conservative about spinning threads
  i32 z = 0;
  i32 nmspinning = AtomicLoad(&S.nmspinning);
  if (nmspinning != 0 || !AtomicCAS(&S.nmspinning, &z, 1)) {
    trace("none (S.npidle>0 but S.nmspinning>0)");
    return;
  }

  // start a new M
  trace("nmspinning=%d", nmspinning);
  p_startm(NULL, /*spinning*/ true);
}


// ===============================================================================================
// M



static void m_init(M* m, i64 id) {
  m->t0.m = m;
  m->t0.atomicstatus = TRunning;
  m->t0.id = AtomicAdd(&S.tidgen, 1);

  mtx_lock(&S.lock);

  if (id >= 0) {
    m->id = id;
  } else {
    m->id = s_reserve_mid();
  }

  m->fastrand[0] = (u32)memhash((uintptr_t)m->id, fastrandseed);
  m->fastrand[1] = (u32)memhash((uintptr_t)nanotime(), ~fastrandseed);
  if ((m->fastrand[0] | m->fastrand[1]) == 0)
    m->fastrand[1] = 1;

  mtx_unlock(&S.lock);
}

// Returns M for the current T, with +1 refcount
static inline M* m_acquire() {
  T* _t_ = t_get();
  _t_->m->locks++;
  return _t_->m;
}

// Release m previosuly m_acquire()'d
static inline void m_release(M* m) {
  m->locks--;
}

static u32 m_fastrand(M* _m_) {
  // [ported from Go runtime]
  // Implement xorshift64+: 2 32-bit xorshift sequences added together.
  // Shift triplet [17,7,16] was calculated as indicated in Marsaglia's
  // Xorshift paper: https://www.jstatsoft.org/article/view/v008i14/xorshift.pdf
  // This generator passes the SmallCrush suite, part of TestU01 framework:
  // http://simul.iro.umontreal.ca/testu01/tu01.html
  u32 s1 = _m_->fastrand[0];
  u32 s0 = _m_->fastrand[1];
  s1 ^= s1 << 17;
  s1 = s1 ^ s0 ^ s1>>7 ^ s0>>16;
  _m_->fastrand[0] = s0;
  _m_->fastrand[1] = s1;
  return s0 + s1;
}

// initsig implements part of m_start1 that only runs on the m0, to initialize signal handlers
static void m0_initsig() {
  trace("TODO");
  // TODO: install signal handlers
}

// m_init_sigstack is called when initializing a new m to set the
// alternate signal stack. If the alternate signal stack is not set
// for the thread (the normal case) then set the alternate signal
// stack to the gsignal stack. If the alternate signal stack is set
// for the thread (the case when a non-Go thread sets the alternate
// signal stack and then calls a Go function) then set the gsignal
// stack to the alternate signal stack. We also set the alternate
// signal stack to the gsignal stack if cgo is not used (regardless
// of whether it is already set). Record which choice was made in
// newSigstack, so that it can be undone in unminit.
// [go: minitSignalStack]
static void m_init_sigstack(M* _m_) {
  // TODO
  // var st stackt
  // sigaltstack(nil, &st)
  // if st.ss_flags&_SS_DISABLE != 0 || !iscgo {
  //   signalstack(&_m_->gsignal.stack)
  //   _m_->newSigstack = true
  // } else {
  //   setGsignalStack(&st, &_m_->goSigStack)
  //   _m_->newSigstack = false
  // }
}

// m_init_sigmask is called when initializing a new m to set the
// thread's signal mask. When this is called all signals have been
// blocked for the thread.  This starts with m.sigmask, which was set
// either from initSigmask for a newly created thread or by calling
// sigsave if this is a non-Go thread calling a Go function. It
// removes all essential signals from the mask, thus causing those
// signals to not be blocked. Then it sets the thread's signal mask.
// After this is called the thread can receive signals.
// [go: minitSignalMask]
static void m_init_sigmask(M* _m_) {
  // TODO
  // nmask := _m_->sigmask
  // for i := range sigtable {
  //   if !blockableSig(uint32(i)) {
  //     sigdelset(&nmask, i)
  //   }
  // }
  // sigprocmask(SIG_SETMASK, &nmask, NULL);
}

// m_start1 is called by m_start
static void NORETURN NO_INLINE m_start1(M* _m_) {
  // iOS does not support alternate signal stack.
  // The signal handler handles it directly.
  #if !(R_TARGET_OS_IOS && R_TARGET_ARCH_ARM64) || R_TARGET_OS_IOS_SIMULATOR
    m_init_sigstack(_m_);
  #endif
  m_init_sigmask(_m_);
  _m_->procid = (u64)thrd_current();

  // Install signal handlers after m_init so that m_init can
  // prepare the thread to be able to handle the signals.
  if (_m_ == &m0)
    m0_initsig();

  // mstartfn is not the coroutine body but a generic function that can
  // be used to run some arbitrary init code on the M thread.
  if (_m_->mstartfn)
    _m_->mstartfn();

  if (_m_ != &m0) {
    p_acquire(_m_->nextp);
    _m_->nextp = NULL;
  } else {
    // Allow sched_spawn to start new Ms.
    mainStarted = true;
  }

  schedule();
  UNREACHABLE;
}

// m_start is the entry-point for new M's. M doesn't have a P yet.
static void NORETURN NO_INLINE m_start(M* _m_) { // [go: runtime.mstart0]
  T* t0 = &_m_->t0;
  assert(t_get() == t0);

  bool osStack = t0->stack.lo == 0;
  if (osStack) {
    // Initialize stack bounds from system stack.
    // Cgo may have left stack size in stack.hi.
    // minit may update the stack bounds.
    //
    // Note: these bounds may not be very accurate.
    // We set hi to &size, but there are things above
    // it. The 1024 is supposed to compensate this,
    // but is somewhat arbitrary.
    uintptr_t size = t0->stack.hi;
    if (size == 0) // main thread:
      size = 8192 * STACK_GUARD_MULTIPLIER;
    t0->stack.hi = (uintptr_t)&size; // uintptr(noescape(unsafe.Pointer(&size)))
    t0->stack.lo = t0->stack.hi - size + 1024;
  }

  trace("t0 stack: [lo=%p - hi=%p] (%zu B)",
    t0->stack.lo, t0->stack.hi, (size_t)(t0->stack.hi - t0->stack.lo));

  // Set up _m_->t0exebuf as a label returning to just after the m_start1 call,
  // for use by [goexit0] and m_call.
  // In m_start1 we're never coming back after we call schedule, so other calls
  // can reuse the current frame. And goexit0 does a gogo that needs to return from m_start1
  // and let this function (m_start) exit the thread.
  if (exectx_save(t0->exectx) == 0)
    m_start1(_m_);

  m_exit(osStack);
}

// p_handoff hands off P from syscall or locked M.
// Always runs without a current P (_t_->m->p==NULL)
static void p_handoff(P* _p_) {
  trace("TODO");
}

// m_exit tears down and exits the current thread.
//
// Don't call this directly to exit the thread, since it must run at
// the top of the thread stack. Instead, use t_switch(&_t_.m.t0) to
// unwind the stack to the point that exits the thread.
//
// It is entered with m->p != NULL, so write barriers are allowed.
// It will release the P before exiting.
static void NORETURN m_exit(bool osStack) {
  M* m = t_get()->m;
  trace("M %p", m);

  if (m == &m0) {
    trace("main thread m0");
    // This is the main thread. Just wedge it.
    //
    // On Linux, exiting the main thread puts the process
    // into a non-waitable zombie state. On Plan 9,
    // exiting the main thread unblocks wait even though
    // other threads are still running. On Solaris we can
    // neither exitThread nor return from mstart. Other
    // bad things probably happen on other platforms.
    //
    // We could try to clean up this M more before wedging
    // it, but that complicates signal handling.
    p_handoff(p_release());
    mtx_lock(&S.lock);
    S.nmfreed++;
    s_checkdeadlock();
    mtx_unlock(&S.lock);
    m_park();
    panic("locked m0 woke up");
  }

  TODO_IMPL;
  UNREACHABLE;
}

// m_call switches from the current T _t_ to the t0 stack and invokes fn(_t_),
// where T is the coroutine that made the call.
//
// It is up to fn to arrange for that later execution, typically by recording
// T in a data structure, causing something to call ready(T) later.
// m_call returns to the original coroutine T later, when T has been rescheduled.
// fn must not return at all; typically it ends by calling schedule, to let the m
// run other coroutines.
//
// m_call can only be called from T stacks (not t0, not gsignal).
//
// If _t_ is to be resumed later, the caller is responsible for saving _t_'s state
// using exectx_save before calling m_call.
//
// fn must never return. It should exectx_resume to keep running T.
//
static void NORETURN m_call(T* _t_, void(*fn)(T*)) {
  trace("T#%llu -> M#%lld (t0)", _t_->id, _t_->m->id);
  T* t0 = &_t_->m->t0;
  assert(_t_ != t0 /* must only m_call from a coroutine, not M/t0 */);
  // update _t_ to t0 and switch execution context to fn
  _tlt = t0;
  // no STACK_TSIZE offset for sp since t0 uses OS stack
  void* sp = (void*)t0->stack.lo + t_stacksize(t0);
  exectx_call((uintptr_t)_t_, (void(*)(uintptr_t))fn, sp); // never returns
}

// m_dofixup runs any outstanding fixup function for the running m.
// Returns true if a fixup was outstanding and actually executed.
static bool m_dofixup() {
  // Not implemented (not currently needed; see Go's mDoFixup)
  return false;
}

// m_park causes a thread to park itself - temporarily waking for
// fixups but otherwise waiting to be fully woken. This is the
// only way that m's should park themselves.
static void m_park() {
  T* _t_ = t_get();
  trace("T#%llu", _t_->id);
  while (1) {
    note_sleep(&_t_->m->park);
    note_clear(&_t_->m->park);
    if (!m_dofixup())
      return;
  }
}

// m_stop stops execution of the current M until new work is available.
// Returns with acquired P.
static void m_stop() {
  T* _t_ = t_get();
  M* m = _t_->m;
  trace("m_stop M#%lld", m->id);
  assert(m->locks == 0); // still has locks
  assert(m->p == 0); // still holding P
  assert(!m->spinning);

  mtx_lock(&S.lock);
  s_midleput(m);
  mtx_unlock(&S.lock);
  m_park();
  p_acquire(_t_->m->nextp);
  _t_->m->nextp = NULL;
}


// ===============================================================================================
// S


// s_mcount returns the number of active M's
static u32 s_mcount() {
  return (u32)(S.mnext - S.nmfreed);
}

// s_reserve_mid returns the next ID to use for a new m.
// This new m is immediately considered 'running' by checkdead.
// S.lock must be held.
static i64 s_reserve_mid() {
  // assertLockHeld(&S.lock);
  if (S.mnext+1 < S.mnext)
    panic("runtime: thread ID overflow");
  i64 id = S.mnext;
  S.mnext++;
  if (s_mcount() > S.maxmcount) {
    errlog("runtime: program exceeds %u-thread limit", S.maxmcount);
    panic("thread exhaustion");
  }
  return id;
}


// Try get a batch of T's from the global runnable queue. S must be locked.
// Returns the top T and moves the rest of T's grabbed to _p_->runq.
static T* s_runqget(P* _p_, u32 max) { // [go globrunqget()]
  if (S.runqsize == 0)
    return NULL;

  // determine amount of Ts to get
  u32 n = MIN(S.runqsize, S.runqsize/S.maxprocs + 1);
  if (max > 0 && n > max)
    n = max;
  // limit number of Ts we take to half of P runq size
  if (n > P_RUNQSIZE / 2) // ok, P_RUNQSIZE is 2^N
    n = P_RUNQSIZE / 2;

  S.runqsize -= n;

  // Take top T, to be returned
  T* tp = TQueuePop(&S.runq);

  // Move n Ts from top of S.runq to end of _p_->runq
  while (--n > 0) {
    T* t = TQueuePop(&S.runq);
    p_runqput(_p_, t, /*next=*/false);
  }

  return tp;
}

// s_runqputhead puts T in global runnable queue head. S must be locked.
void s_runqputhead(T* t) {
  TQueuePush(&S.runq, t);
  S.runqsize++;
}

// s_checkdeadlock checks for deadlock situation.
// The check is based on number of running M's, if 0 -> deadlock.
static void s_checkdeadlock() { // [go checkdead()]
  TODO_IMPL;
}

// s_pidleget tries to get a P from S.pidle list. S must be locked.
static P* nullable s_pidleget() {
  P* p = S.pidle;
  if (p != NULL) {
    vbm_set(&timerpMask, p->id);
    vbm_clear(&idlepMask, p->id);
    S.pidle = p->link;
    AtomicSub(&S.npidle, 1);
  }
  return p;
}

// p_update_timerpMask clears pp's timer mask if it has no timers on its heap.
//
// Ideally, the timer mask would be kept immediately consistent on any timer
// operations. Unfortunately, updating a shared global data structure in the
// timer hot path adds too much overhead in applications frequently switching
// between no timers and some timers.
//
// As a compromise, the timer mask is updated only on pidleget / pidleput. A
// running P (returned by pidleget) may add a timer at any time, so its mask
// must be set. An idle P (passed to pidleput) cannot add new timers while
// idle, so if it has no timers at that time, its mask may be cleared.
//
// Thus, we get the following effects on timer-stealing in findrunnable:
//
// * Idle Ps with no timers when they go idle are never checked in findrunnable
//   (for work- or timer-stealing; this is the ideal case).
// * Running Ps must always be checked.
// * Idle Ps whose timers are stolen must continue to be checked until they run
//   again, even after timer expiration.
//
// When the P starts running again, the mask should be set, as a timer may be
// added at any time.
//
// TODO(prattmic): Additional targeted updates may improve the above cases.
// e.g., updating the mask when stealing a timer.
static void p_update_timerpMask(P* p) {
  if (AtomicLoad(&p->numTimers) > 0)
    return;
  // Looks like there are no timers, however another P may transiently
  // decrement numTimers when handling a timerModified timer in
  // checkTimers. We must take timersLock to serialize with these changes.
  mtx_lock(&p->timersLock);
  if (AtomicLoad(&p->numTimers) == 0) {
    vbm_clear(&timerpMask, p->id);
  }
  mtx_unlock(&p->timersLock);
}

// s_pidleput puts P to on S.pidle list. S must be locked.
static void s_pidleput(P* p) {
  assert(p_runqempty(p) /* trying to put P to sleep with runnable Ts */);
  p_update_timerpMask(p);
  vbm_set(&idlepMask, p->id);
  p->link = S.pidle;
  S.pidle = p;
  AtomicAdd(&S.npidle, 1);
}

// s_midleput puts an M on the midle list. S must be locked.
static void s_midleput(M* m) { // [go: mput()]
  m->schedlink = S.midle;
  S.midle = m;
  S.midlecount++;
  s_checkdeadlock();
}

// Try to get an m from midle list. S must be locked.
static M* s_midleget() {
  // assertLockHeld(&S.lock);
  M* m = S.midle;
  if (m != NULL) {
    S.midle = m->schedlink;
    S.midlecount--;
  }
  return m;
}

// Allocate a new m unassociated with any thread.
// Can use p for allocation context if needed.
// mstartfn is recorded as the new M's m.mstartfn.
// id is a optional pre-allocated M ID. Omit by passing -1 (one will be allocated for you).
static M* s_allocm(P* _p_, void(*mstartfn)(void), i64 id) {
  T* _t_ = t_get();
  m_acquire(); // disable GC because it can be called from sysmon
  if (_t_->m->p == NULL)
    p_acquire(_p_); // temporarily borrow p for mallocs in this function

  // note: Go separates memory of T and its stack; here it calls stackfree to reliquish
  // the stack. However we use a T's stack to store T.
  // Additionally M.t0 doesn't have a separate stack; it uses the OS-provided thread stack
  // of its M.
  // Translated code kept here below for future reference:
  // // Release the free M list. We need to do this somewhere
  // // and this may free up a stack we can use.
  // if (S.freem) {
  //   mtx_lock(&S.lock);
  //   M* newList = NULL;
  //   for (M* freem = S.freem; freem;) {
  //     if (freem->freewait != 0) {
  //       // skip to next in list
  //       M* next = freem->freelink;
  //       freem->freelink = newList;
  //       newList = freem;
  //       freem = next;
  //       continue;
  //     }
  //     stackfree(freem->t0.stack);
  //     freem = freem->freelink;
  //   }
  //   S.freem = newList;
  //   mtx_unlock(&S.lock);
  // }

  M* mp = memalloct(MemLibC(), M);
  mp->mstartfn = mstartfn;
  m_init(mp, id);
  mp->t0.m = mp;

  if (_p_ == _t_->m->p)
    p_release();

  m_release(_t_->m);

  return mp;
}


// m_start_stub is the OS thread entry point
static void* m_start_stub(M* m) {
  _tlt = &m->t0;
  m_start(m);
  return NULL;
}


static void spawn_osthread(M* mp) {
  pthread_attr_t attr;
  int err = pthread_attr_init(&attr);
  if (err != 0)
    panic("pthread_attr_init");

  // Find out OS stack size for our own stack guard.
  uintptr_t stacksize = 0;
  if (pthread_attr_getstacksize(&attr, &stacksize) != 0)
    panic("pthread_attr_getstacksize");

  trace("OS thread stack size: %zu B", stacksize);
  mp->t0.stack.hi = stacksize; // for m_start

  // Tell the pthread library we won't join with this thread.
  if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0)
    panic("pthread_attr_setdetachstate");

  // Finally, create the thread.
  // It starts at mstart_stub, which does some low-level setup and then calls mstart
  SigSet oset;
  SigSet sigset_all;
  memset(&sigset_all, 0xff, sizeof(SigSet));
  sigprocmask(SIG_SETMASK, &sigset_all, &oset);
  pthread_t tid;
  err = pthread_create(&tid, &attr, (void*_Nullable(*_Nonnull)(void*_Nullable))m_start_stub, mp);
  sigprocmask(SIG_SETMASK, &oset, NULL);
  if (err != 0)
    panic("pthread_create");
}


// s_newm creates & spawns a new M.
// It will start off with a call to fn, or the scheduler if NULL.
// May run with m.p==NULL.
// id is optional pre-allocated m ID. Omit by passing -1.
static void s_newm(P* _p_, void(*fn)(void), i64 id) {
  M* mp = s_allocm(_p_, fn, id);
  mp->doespark = _p_ != NULL;
  mp->nextp = _p_;
  mp->sigmask = initSigmask;
  trace("M#%llu", mp->id);
  rwmtx_rlock(&execLock); // Prevent process clone
  spawn_osthread(mp);
  rwmtx_runlock(&execLock);
}


// p_init initializes a newly allocated P
static void p_init(P* p, u32 id) {
  p->id = id;
  p->status = PIdle;

  // This P may get timers when it starts running. Set the mask here
  // since the P may not go through pidleget (notably P 0 on startup).
  vbm_set(&timerpMask, id);

  // Similarly, we may not go through pidleget before this P starts
  // running if it is P 0 on startup.
  vbm_clear(&idlepMask, id);

  mtx_init(&p->timersLock, mtx_plain);
}

// s_procresize changes the number of processors.
// S.lock must be locked.
// The world is stopped.
// Returns list of Ps with local work; they need to be scheduled by the caller.
static P* s_procresize(u32 nprocs) {
  trace("S.maxprocs=%u, nprocs=%u", S.maxprocs, nprocs);
  u32 old = S.maxprocs;
  assert(nprocs > 0);
  assert(nprocs <= COMAXPROCS_MAX);
  assert(old <= COMAXPROCS_MAX);

  // grow allp if needed
  if (nprocs > S.maxprocs) {
    trace("grow allp");
    // Synchronize with retake, which could be running concurrently since it doesn't run on a P
    mtx_lock(&S.allplock);
    vbm_resize(&idlepMask, nprocs);
    vbm_resize(&timerpMask, nprocs);
    mtx_unlock(&S.allplock);
  }

  // initialize new P's
  for (u32 i = old; i < nprocs; i++) {
    P* p = S.allp[i];
    if (p == NULL) {
      p = memalloct(MemLibC(), P);
      p_init(p, i);
      AtomicStore( (_Atomic(P*)*)&S.allp[i], p );
    }
  }

  // fetch current T & M
  T* _t_ = t_get();
  M* _m_ = _t_->m;
  assert(_m_ != NULL);

  // associate S.allp[0] with current M (if needed) and set P's status to PRunning
  if (_m_->p != 0 && _m_->p->id < nprocs) {
    // continue to use the current P
    _m_->p->status = PRunning;
    // _m_->p->mcache.prepareForSweep()
  } else {
    // release the current P and acquire allp[0].
    //
    // We must do this before destroying our current P
    // because p.destroy itself has write barriers, so we
    // need to do that from a valid P.
    if (_m_->p != NULL) {
      _m_->p->m = NULL;
      _m_->p = NULL;
    }
    P* p = S.allp[0];
    p->m = 0;
    p->status = PIdle;
    p_acquire(p); // associate P and current M (p->m=m, m->p=p, p.status=PRunning)
  }

  // free unused P's
  // In Go, this is implemented in a separate function (*p).destroy()
  for (u32 i = nprocs; i < old; i++) {
    assert(S.allp[i] != NULL);
    P* p = S.allp[i];

    // move all runnable tasks to the global queue
    while (p->runqhead != p->runqtail) {
      // pop from tail of local queue
      --p->runqtail;
      T* t = p->runq[p->runqtail % P_RUNQSIZE];
      // push onto head of global queue
      s_runqputhead(t);
    }
    if (p->runnext != NULL) {
      s_runqputhead(p->runnext);
      p->runnext = NULL;
    }
    // move p.tfree to S.tfree
    p_tfree_purge(p);
    p->status = PDead;
    // can't free P itself because it can be referenced by an M in syscall
  }

  // Trim allp
  if (nprocs < S.maxprocs) {
    mtx_lock(&S.allplock);
    vbm_resize(&idlepMask, nprocs);
    vbm_resize(&timerpMask, nprocs);
    mtx_unlock(&S.allplock);
  }

  // build list of runnable Ps
  P* runnablePs = NULL; // head of linked list
  u32 i = nprocs;
  while (i > 0) {
    P* p = S.allp[--i];
    if (_m_->p == p)
      continue;
    p->status = PIdle;
    if (p_runqempty(p)) {
      s_pidleput(p);
    } else {
      p->m = s_midleget();
      p->link = runnablePs;
      runnablePs = p;
    }
  }

  randord_reset(&stealOrder, nprocs);

  // update maxprocs to the number of Ps now available
  S.maxprocs = nprocs;

  // full store & load, pre & post memory fence, as we changed allp and maxprocs
  atomic_thread_fence(memory_order_seq_cst);

  return runnablePs;
}


// sched_spawn creates a new T running fn with argsize bytes of arguments.
// stacksize is a requested minimum number of bytes to allocate for its stack. A stacksize
// of 0 means to allocate a stack of default standard size.
// If stackmem is not NULL, T and its stack will use that memory instead of memory managed
// by the scheduler. The caller will be responsible for freeing that memory after the task ends.
// Put it on the queue of T's waiting to run.
// The compiler turns a go statement into a call to this.
int sched_spawn(EntryFun fn, uintptr_t arg1, void* stackmem, size_t stacksize) {
  T* _t_ = t_get();
  assert(fn != NULL);

  // disable preemption because it can be holding p in a local var
  m_acquire();

  P* _p_ = _t_->m->p;
  T* newt = NULL;
  if (stackmem != NULL) {
    // user-provided memory. Align as needed.
    uintptr_t lo = (uintptr_t)stackmem;
    lo = align2(lo, STACK_ALIGN);
    stacksize = stacksize - (lo - (uintptr_t)stackmem);
    if (stacksize < STACK_MIN) {
      errno = EINVAL; // "Invalid argument"
      return -1;
    }
    newt = t_init((u8*)lo, stacksize);
    newt->fl |= TFlUserStack;
    allt_add(newt);
  } else {
    // managed memory
    if (stacksize == 0 || stacksize == STACK_SIZE_DEFAULT) {
      // default stack size
      newt = p_tfree_get(_p_);
      if (newt != NULL)
        trace("got a spare task from p_tfree_get(_p_) => %p", newt);
    } // else: custom stacksize (the T won't end up on tfree)
    if (newt == NULL) {
      newt = t_alloc(stacksize);
      t_setstatus(newt, TDead); // t_casstatus(newt, TIdle, TDead);
      allt_add(newt);
    }
  }

  void* sp = (void*)newt; // T is allocated at the top of the stack
  // trace("setup sp %p (T %p)", sp, newt);
  exectx_setup(newt->exectx, fn, arg1, sp);

  assert(newt->stack.hi != 0 /* else: newt missing stack */);
  assert(t_readstatus(newt) == TDead);

  newt->id = AtomicAdd(&S.tidgen, 1);
  t_casstatus(newt, TDead, TRunnable);

  m_release(_t_->m); // re-enable preemption

  assert(t_get()->m->p == _p_);

  p_runqput(_p_, newt, true /* = puts T in _p_->runnext */);
  trace("added T#%llu to P#%u runq", newt->id, _p_->id);
  // note: mainStarted=true is set in Go by the main coroutine (runtime.main)
  if (mainStarted)
    p_wake();
  return 0;
}

// s_stealwork attempts to steal work from other P's
static inline T* s_stealwork(T* _t_, bool* inheritTime, bool* ranTimer) {
  M* m = _t_->m;
  if (!m->spinning) {
    trace("marking M#%llu spinning", m->id);
    m->spinning = true;
    AtomicAdd(&S.nmspinning, 1);
  }

  P* _p_ = m->p;
  const int stealTries = 4;

  for (int i = 0; i < stealTries; i++) {
    bool stealTimersOrRunNextT = i == stealTries-1; // is last steal attempt?
    RandomEnum e = randord_start(&stealOrder, m_fastrand(m));
    for (; !randenum_done(&e); randenum_next(&e)) {
      // Pick a random P
      P* p2 = S.allp[randenum_pos(&e)];
      if (_p_ == p2)
        continue;

      // Steal timers from p2. This call to checkTimers is the only place
      // where we might hold a lock on a different P's timers. We do this
      // once on the last pass before checking runnext because stealing
      // from the other P's runnext should be the last resort, so if there
      // are timers to steal do that first.
      //
      // We only check timers on one of the stealing iterations because
      // the time stored in now doesn't change in this loop and checking
      // the timers for each P more than once with the same value of now
      // is probably a waste of time.
      //
      // timerpMask tells us whether the P may have timers at all. If it
      // can't, no need to check at all.
      if (stealTimersOrRunNextT && vbm_read(&timerpMask, randenum_pos(&e))) {
        trace("TODO: checkTimers");
        // tnow, w, ran := checkTimers(p2, now)
        // now = tnow
        // if w != 0 && (pollUntil == 0 || w < pollUntil) {
        //   pollUntil = w
        // }
        // if ran {
        //   // Running the timers may have
        //   // made an arbitrary number of G's
        //   // ready and added them to this P's
        //   // local run queue. That invalidates
        //   // the assumption of runqsteal
        //   // that is always has room to add
        //   // stolen G's. So check now if there
        //   // is a local G to run.
        //   if gp, inheritTime := runqget(_p_); gp != nil {
        //     return gp, inheritTime
        //   }
        //   ranTimer = true
        // }
      }

      // Don't bother to attempt to steal if p2 is idle.
      if (!vbm_read(&idlepMask, randenum_pos(&e))) {
        // trace("try steal from P#%u", p2->id);
        T* t = p_runqsteal(_p_, p2, stealTimersOrRunNextT);
        if (t) {
          *inheritTime = false;
          trace("found %p, %p", t, p2);
          trace("found T#%llu in P#%u", t->id, p2->id);
          return t;
        }
      } else {
        trace("skip trying steal from non-idle P#%u", p2->id);
      }

    } // for (; !randenum_done(&e); randenum_next(&e))
  } // for (int i = 0; i < stealTries; i++)
  return NULL;
}

// s_findrunnable finds a runnable coroutine to execute.
// Tries to steal from other P's, get T from global queue, poll network.
static T* s_findrunnable(bool* inheritTime) {
  T* _t_ = t_get();
  trace("_t_ T#%llu", _t_->id);

top: {}
  P* _p_ = _t_->m->p;
  // timers
  // TODO: now, pollUntil, _ := checkTimers(_p_, 0)
  u64 now = 0; // second arg to checkTimers returned when there are no timers
  i64 pollUntil = 0;

  // local runq
  trace("try local runq");
  T* tp = p_runqget(_p_, inheritTime);
  if (tp != NULL)
    return tp;

  // global runq
  trace("try global runq");
  if (S.runqsize != 0) {
    mtx_lock(&S.lock);
    T* tp = s_runqget(_p_, /*max*/0);
    mtx_unlock(&S.lock);
    if (tp) {
      *inheritTime = false;
      return tp;
    }
  }

  trace("TODO: netpoll");

  // Steal work from other P's
  trace("try steal from other P's");
  //
  // If number of spinning M's >= number of busy P's, block.
  // This is necessary to prevent excessive CPU consumption
  // when COMAXPROCS>>1 but the program parallelism is low.
  if (!_t_->m->spinning &&
      2*AtomicLoad(&S.nmspinning) >= (i32)(S.maxprocs - AtomicLoad(&S.npidle)))
  {
    goto stop;
  }
  bool ranTimer = false;
  T* t = s_stealwork(_t_, inheritTime, &ranTimer);
  if (t != NULL)
    return t;
  // Running a timer may have made some goroutine ready
  if (ranTimer)
    goto top; // retry while loop

stop: {}
  trace("stop; no work");

  i64 delta = -1;
  if (pollUntil != 0) {
    // checkTimers ensures that polluntil > now.
    delta = pollUntil - (i64)now;
  }

  // Before we drop our P, make a snapshot of the allp slice, which can change underfoot
  // once we no longer block safe-points. We don't need to snapshot the contents because
  // everything up to cap(allp) is immutable.
  auto allpLenSnapshot = S.maxprocs;
  // Also snapshot masks. Value changes are OK, but we can't allow
  // len to change out from under us.
  auto idlepMaskSnapshot = idlepMask;
  auto timerpMaskSnapshot = timerpMask;

  // return P and block
  mtx_lock(&S.lock);
  if (S.runqsize != 0) {
    T* t = s_runqget(_p_, 0);
    mtx_unlock(&S.lock);
    trace("found T#%llu in s_runqget(P#%u)", t->id, _p_->id);
    *inheritTime = false;
    return t;
  }
  assert(p_release() == _p_);
  s_pidleput(_p_);
  mtx_unlock(&S.lock);

  // Delicate dance: thread transitions from spinning to non-spinning state,
  // potentially concurrently with submission of new goroutines. We must
  // drop nmspinning first and then check all per-P queues again (with
  // #StoreLoad memory barrier in between). If we do it the other way around,
  // another thread can submit a goroutine after we've checked all run queues
  // but before we drop nmspinning; as a result nobody will unpark a thread
  // to run the goroutine.
  // If we discover new work below, we need to restore m.spinning as a signal
  // for m_resetspinning to unpark a new worker thread (because there can be more
  // than one starving goroutine). However, if after discovering new work
  // we also observe no idle Ps, it is OK to just park the current thread:
  // the system is fully loaded so no spinning threads are required.
  // Also see "Worker thread parking/unparking" comment at the top of the file.
  bool wasSpinning = _t_->m->spinning;
  if (wasSpinning) {
    _t_->m->spinning = false;
    // if int32(atomic.Xadd(&S.nmspinning, -1)) < 0
    if (AtomicSub(&S.nmspinning, 1) - 1 < 0)
      panic("s_findrunnable: negative nmspinning");
  }

  // check all runqueues once again
  // for id, _p_ := range allpSnapshot
  for (u32 id = 0; id < allpLenSnapshot; id++) {
    P* _p_ = S.allp[id];
    if (!vbm_read(&idlepMaskSnapshot, id) && !p_runqempty(_p_)) {
      mtx_lock(&S.lock);
      _p_ = s_pidleget();
      mtx_unlock(&S.lock);
      if (_p_ != NULL) {
        p_acquire(_p_);
        if (wasSpinning) {
          _t_->m->spinning = true;
          AtomicAdd(&S.nmspinning, 1);
        }
        trace("found idle P#%u -- retrying", _p_->id);
        goto top;
      }
      break;
    }
  }

  // Similar to above, check for timer creation or expiry concurrently with
  // transitioning from spinning to non-spinning. Note that we cannot use
  // checkTimers here because it calls adjusttimers which may need to allocate
  // memory, and that isn't allowed when we don't have an active P.
  for (u32 id = 0; id < allpLenSnapshot; id++) {
    // P* _p_ = S.allp[id];
    if (vbm_read(&timerpMaskSnapshot, id)) {
      TODO_IMPL;
      //w := nobarrierWakeTime(_p_)
      //if w != 0 && (pollUntil == 0 || w < pollUntil) {
      //  pollUntil = w
      //}
    }
  }
  if (pollUntil != 0) {
    if (now == 0)
      now = nanotime();
    delta = pollUntil - (i64)now;
    if (delta < 0)
      delta = 0;
  }

  // // TODO: poll network
  // if (netpollinited() &&
  //     (atomic.Load(&netpollWaiters) > 0 || pollUntil != 0) &&
  //     atomic.Xchg64(&sched.lastpoll, 0) != 0)
  // {
  //   // atomic.Store64(&sched.pollUntil, uint64(pollUntil))
  //   // ...
  // }

  m_stop();
  goto top;

  UNREACHABLE;
} // s_findrunnable


static void m_resetspinning(M* m) {
  trace("m_resetspinning M#%lld", m->id);
  assert(t_get()->m == m);
  assert(m->spinning);
  m->spinning = false;
  i32 nmspinning = AtomicSub(&S.nmspinning, 1) - 1;
  if (nmspinning < 0)
    panic("m_findrunnable: negative nmspinning");
  // M wakeup policy is deliberately somewhat conservative, so check if we
  // need to wakeup another P here. See "Worker thread parking/unparking"
  // comment at the top of the file for details.
  p_wake();
}


// schedule performs one pass of scheduling: find a runnable coroutine and execute it.
// Never returns.
static void NORETURN schedule() {
  T* _t_ = t_get();
  M* m = _t_->m;
  trace("_t_ T#%llu on M#%lld", _t_->id, m->id);

  assert(m->locks == 0);

  if (m->lockedt) {
    TODO_IMPL;
    // stoplockedm()
    // execute(m->lockedt, false) // Never returns.
  }

  // top: {}
  P* pp = m->p;
  pp->preempt = false;

  // Sanity check: if we are spinning, the run queue should be empty.
  // Check this before calling checkTimers, as that might call
  // goready to put a ready coroutine on the local run queue.
  if (m->spinning && (pp->runnext != NULL || pp->runqhead != pp->runqtail))
    panic("schedule: spinning with local work");

  // TODO: checkTimers(pp, 0);

  T* t = NULL;
  bool inheritTime = false;

  // Check the global runnable queue once in a while to ensure fairness.
  // Otherwise two coroutines can completely occupy the local runqueue
  // by constantly respawning each other.
  if (pp->schedtick % 61 == 0 && S.runqsize > 0) {
    trace("random global runq steal attempt");
    mtx_lock(&S.lock);
    t = s_runqget(pp, 1);
    mtx_unlock(&S.lock);
    if (t)
      trace("found T#%llu with s_runqget", t->id);
  }

  if (t == NULL) {
    trace("try p_runqget P#%u", pp->id);
    t = p_runqget(pp, &inheritTime);
    // We can see t != NULL here even if the M is spinning,
    // if checkTimers added a local goroutine via goready.
    if (t)
      trace("found T#%llu with p_runqget", t->id);
  }

  if (t == NULL) {
    trace("try s_findrunnable");
    t = s_findrunnable(&inheritTime); // blocks until work is available
    assert(t != NULL);
    trace("found T#%llu with s_findrunnable", t->id);
  }

  // This thread is going to run a coroutine and is not spinning anymore,
  // so if it was marked as spinning we need to reset it now and potentially
  // start a new spinning M.
  if (m->spinning)
    m_resetspinning(m);

  // TODO:
  if (t->lockedm != NULL) {
    // // Hands off own p to the locked m, then blocks waiting for a new p.
    TODO_IMPL;
    // startlockedm(gp);
    // goto top;
  }

  t_execute(t, inheritTime); // never returns
  UNREACHABLE;
}


// sigsave saves the current thread's signal mask into *p.
// This is used to preserve the non-Go signal mask when a non-Go thread calls a Go function.
// This is called by needm which may be called on a non-Go thread with no T available.
static void sigsave(SigSet* p);
//
// sigrestore sets the current thread's signal mask to sigmask.
// This is used to restore the non-Go signal mask when a non-Go thread calls a Go function.
// This is called by s_dropm after T has been cleared.
static void sigrestore(const SigSet* sigmask);

#if R_TARGET_OS_POSIX
  static void sigsave(SigSet* p) {
    sigprocmask(SIG_SETMASK, NULL, p);
  }
  static void sigrestore(const SigSet* sigmask) {
    sigprocmask(SIG_SETMASK, sigmask, NULL);
  }
#else
  #error "TODO signal functions for this target"
#endif


// void fctx_test();

// sched_init bootstraps the scheduler.
// The OS thread it is called on will be bound to m0
static void sched_init() {
  // The Go bootstrap sequence is:
  //  call osinit
  //  call schedinit
  //  make & queue new G
  //    this happens in asm_ARCH.s runtime·rt0_go
  //    CALL runtime·newproc(runtime·main)
  //  call runtime·mstart
  //    CALL runtime·mstart()
  //

  memset(&allt, 0, sizeof(allt));
  mtx_init(&allt.lock, mtx_plain);
  rwmtx_init(&execLock, mtx_plain);
  mtx_init(&S.lock, mtx_plain);
  mtx_init(&S.allplock, mtx_plain);
  mtx_init(&S.tfree.lock, mtx_plain);

  fastrandinit(); // must be done before m_init
  randord_init(&stealOrder);

  // must set maxmcount before m_init is called
  S.maxmcount = 10000; // number from go's schedinit in proc.go

  // main thread M
  m_init(&m0, -1);
  _tlt = &m0.t0; // Set current task to root task of m0
  T* _t_ = &m0.t0;

  sigsave(&_t_->m->sigmask);
  initSigmask = _t_->m->sigmask;

  // nprocs (number of P's)
  u32 nprocs = 0;
  const char* str = getenv("COMAXPROCS");
  if (!str || !parseu32(str, strlen(str), 10, &nprocs) || nprocs < 1)
    nprocs = os_ncpu();

  trace("COMAXPROCS=%u", nprocs);

  S.lastpoll = nanotime();

  mtx_lock(&S.lock);
  s_procresize(MAX(1, nprocs));
  mtx_unlock(&S.lock);
}

// sched_main is the API entry point. sched_init must have been called already.
// This function creates a new coroutine with fn as the body and then enters the
// (continuation passing) scheduler loop on the calling thread.
void NORETURN sched_main(EntryFun fn, uintptr_t arg1) {
  sched_init();
  sched_spawn(fn, arg1, /*stackmem*/NULL, /*stacksize*/ 0);
  t1 = m0.p->runnext;
  m_start(&m0); // calls schedule(); never returns
  UNREACHABLE;
}
