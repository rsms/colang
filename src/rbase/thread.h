// Portable C11 threads
#pragma once

#if defined(__STDC_NO_THREADS__) && __STDC_NO_THREADS__
  #include <pthread.h>

  #define ONCE_FLAG_INIT  PTHREAD_ONCE_INIT

  typedef pthread_t       thrd_t;
  typedef pthread_mutex_t mtx_t;
  typedef pthread_cond_t  cnd_t;
  typedef pthread_key_t   tss_t;
  typedef pthread_once_t  once_flag;

  typedef int  (*thrd_start_t)(void*);
  typedef void (*tss_dtor_t)(void*);

  enum { // bitflags
    mtx_plain     = 0,
    mtx_recursive = 1,
    mtx_timed     = 2,
  };

  enum {
    thrd_success,
    thrd_timedout,
    thrd_busy,
    thrd_error,
    thrd_nomem
  };

  #include "thread_pthread.h"

  #define THRD_NULLABLE nullable
#else
  #include <threads.h>
  #define THRD_NULLABLE
#endif

// function overview:
//
// int    thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
// void   thrd_exit(int res);
// int    thrd_join(thrd_t thr, int *res);
// int    thrd_detach(thrd_t thr);
// thrd_t thrd_current(void);
// int    thrd_equal(thrd_t a, thrd_t b);
// int    thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out);
// void   thrd_yield(void);
//
// int    mtx_init(mtx_t *mtx, int type);
// void   mtx_destroy(mtx_t *mtx);
// int    mtx_lock(mtx_t *mtx);
// int    mtx_trylock(mtx_t *mtx);
// int    mtx_timedlock(mtx_t *mtx, const struct timespec *ts);
// int    mtx_unlock(mtx_t *mtx);
//
// int    cnd_init(cnd_t *cond);
// void   cnd_destroy(cnd_t *cond);
// int    cnd_signal(cnd_t *cond);
// int    cnd_broadcast(cnd_t *cond);
// int    cnd_wait(cnd_t *cond, mtx_t *mtx);
// int    cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);
//
// int    tss_create(tss_t *key, tss_dtor_t dtor);
// void   tss_delete(tss_t key);
// int    tss_set(tss_t key, void *val);
// void*  tss_get(tss_t key);
//
// void   call_once(once_flag *flag, void (*func)(void));
//

// rwmtx_t is a read-write mutex.
// There can be many concurrent readers but only one writer.
// While no write lock is held, up to 16777214 read locks may be held.
// While a write lock is held no read locks or other write locks can be held.
typedef struct rwmtx_t {
  mtx_t      w; // writer lock
  atomic_u32 r; // reader count
} rwmtx_t;
static int  rwmtx_init(rwmtx_t* m, int wtype);
static void rwmtx_destroy(rwmtx_t* m);
int rwmtx_rlock(rwmtx_t* m);     // acquire read-only lock (blocks until acquired)
int rwmtx_tryrlock(rwmtx_t* m);  // attempt to acquire read-only lock (non-blocking)
int rwmtx_runlock(rwmtx_t* m);   // release read-only lock
int rwmtx_lock(rwmtx_t* m);      // acquire read+write lock (blocks until acquired)
int rwmtx_trylock(rwmtx_t* m);   // attempt to acquire read+write lock (non-blocking)
int rwmtx_unlock(rwmtx_t* m);    // release read+write lock

// ----------------------------------------------------------------------------
//  inline implementations

static inline int rwmtx_init(rwmtx_t* m, int wtype) {
  assert(wtype != mtx_timed /* not supported */);
  AtomicStore(&m->r, 0);
  return mtx_init(&m->w, wtype);
}
static inline void rwmtx_destroy(rwmtx_t* m) { mtx_destroy(&m->w); }


