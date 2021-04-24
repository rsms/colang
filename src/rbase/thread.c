#include "rbase.h"


// MTX_W_WATERMARK: this is a watermark value for rwmtx_t.r
//   rwmtx_t.r == 0                -- no read or write locks
//   rwmtx_t.r <  MTX_W_WATERMARK  -- rwmtx_t.r read locks
//   rwmtx_t.r >= MTX_W_WATERMARK  -- write lock held
// rwmtx_rlock optimistically increments rwmtx_t.r thus the value of rwmtx_t.r
// may exceed MTX_W_WATERMARK for brief periods of time while a rwmtx_rlock fails.
const u32 MTX_W_WATERMARK = 0xffffff;

int rwmtx_rlock(rwmtx_t* m) {
  while (1) {
    u32 r = atomic_fetch_add_explicit(&m->r, 1, memory_order_acquire);
    if (r < MTX_W_WATERMARK)
      return thrd_success;
    // there's a write lock; revert addition and await write lock
    atomic_fetch_sub_explicit(&m->r, 1, memory_order_release);
    int status = mtx_lock(&m->w);
    if (status != thrd_success)
      return status;
    mtx_unlock(&m->w);
    // try read lock again
  }
}

int rwmtx_tryrlock(rwmtx_t* m) {
  while (1) {
    u32 r = atomic_fetch_add_explicit(&m->r, 1, memory_order_acquire);
    if (r < MTX_W_WATERMARK)
      return thrd_success;
    // there's a write lock; revert addition and await write lock
    atomic_fetch_sub_explicit(&m->r, 1, memory_order_release);
    return thrd_busy;
  }
}

int rwmtx_runlock(rwmtx_t* m) {
  while (1) {
    u32 prevr = atomic_load_explicit(&m->r, memory_order_acquire);
    if (prevr == 0)
      return thrd_error; // not holding a read lock!
    if (prevr < MTX_W_WATERMARK) {
      atomic_fetch_sub_explicit(&m->r, 1, memory_order_release);
      return thrd_success;
    }
    // await write lock
    int status = mtx_lock(&m->w);
    if (status != thrd_success)
      return status;
    mtx_unlock(&m->w);
  }
}

int rwmtx_lock(rwmtx_t* m) {
  int retry = 0;
  while (1) {
    u32 prevr = atomic_load_explicit(&m->r, memory_order_acquire);
    if (prevr == 0 &&
        atomic_compare_exchange_weak_explicit(
          &m->r, &prevr, MTX_W_WATERMARK, memory_order_release, memory_order_acquire))
    {
      // no read locks; acquire write lock
      return mtx_lock(&m->w);
    }
    // spin
    if (retry++ == 100) {
      retry = 0;
      thrd_yield();
    }
  }
}

int rwmtx_trylock(rwmtx_t* m) {
  while (1) {
    u32 prevr = atomic_load_explicit(&m->r, memory_order_acquire);
    if (prevr == 0 &&
        atomic_compare_exchange_weak_explicit(
          &m->r, &prevr, MTX_W_WATERMARK, memory_order_release, memory_order_acquire))
    {
      // no read locks; acquire write lock
      return mtx_trylock(&m->w);
    }
    return thrd_busy;
  }
}


int rwmtx_unlock(rwmtx_t* m) {
  int retry = 0;
  while (1) {
    u32 prevr = atomic_load_explicit(&m->r, memory_order_acquire);
    if (prevr >= MTX_W_WATERMARK &&
        atomic_compare_exchange_weak_explicit(
          &m->r, &prevr, prevr - MTX_W_WATERMARK, memory_order_release, memory_order_acquire))
    {
      return mtx_unlock(&m->w);
    }
    if (prevr < MTX_W_WATERMARK)
      return thrd_error; // not holding a write lock!
    // spin
    if (retry++ == 100) {
      retry = 0;
      thrd_yield();
    }
  }
}

// -----------------------------------------------------------------------------------------------
#if R_UNIT_TEST_ENABLED

typedef struct TestThread {
  thrd_t      t;  // read-only
  u32         id; // read-only
  rwmtx_t*    rwmu;
  atomic_u32* rcount; // current number of writes
  atomic_u32* wcount; // current number of writes
  atomic_u32* rcount_while_writing; // value of rcount while writing
  atomic_u32* wcount_while_reading; // value of wcount while writing
} TestThread;

static int test_thread(void* arg) {
  TestThread* t = (TestThread*)arg;
  rwmtx_t* rwmu = t->rwmu;
  int iterations = 30;

  if (t->id % 2 == 0) {
    for (int i = 0; i < iterations; i++) {
      // dlog("T(%u) acquire read lock", t->id);
      asserteq(rwmtx_rlock(rwmu), thrd_success);
      atomic_fetch_add_explicit(t->rcount, 1, memory_order_acq_rel);
      // dlog("T(%u) read", t->id);
      auto wcount = atomic_load_explicit(t->wcount, memory_order_acquire);
      atomic_fetch_add_explicit(t->wcount_while_reading, wcount, memory_order_acq_rel);
      thrd_yield();
      atomic_fetch_sub_explicit(t->rcount, 1, memory_order_acq_rel);
      asserteq(rwmtx_runlock(rwmu), thrd_success);
      // dlog("T(%u) released read lock", t->id);
    }
  } else {
    for (int i = 0; i < iterations; i++) {
      // dlog("T(%u) acquire write lock", t->id);
      asserteq(rwmtx_lock(rwmu), thrd_success);
      atomic_fetch_add_explicit(t->wcount, 1, memory_order_acq_rel);
      // dlog("T(%u) write", t->id);
      auto rcount = atomic_load_explicit(t->rcount, memory_order_acquire);
      atomic_fetch_add_explicit(t->rcount_while_writing, rcount, memory_order_acq_rel);
      thrd_yield();
      atomic_fetch_sub_explicit(t->wcount, 1, memory_order_acq_rel);
      asserteq(rwmtx_unlock(rwmu), thrd_success);
      // dlog("T(%u) released read lock", t->id);
    }
  }
  return 0;
}


R_UNIT_TEST(rwmtx_basics) {
  rwmtx_t rwmu;
  rwmtx_init(&rwmu, mtx_plain);

  // multiple concurrent readers
  for (int i = 0; i < 4; i++) {
    asserteq(rwmtx_rlock(&rwmu), thrd_success);
  }
  for (int i = 0; i < 4; i++) {
    asserteq(rwmtx_runlock(&rwmu), thrd_success);
  }
  asserteq(rwmtx_runlock(&rwmu), thrd_error); // no read lock held

  // exclusive writers
  for (int i = 0; i < 4; i++) {
    asserteq(rwmtx_lock(&rwmu), thrd_success);
    asserteq(rwmtx_unlock(&rwmu), thrd_success);
  }

  // trylock
  asserteq(rwmtx_lock(&rwmu), thrd_success);
  asserteq(rwmtx_trylock(&rwmu), thrd_busy);  // write lock held already
  asserteq(rwmtx_tryrlock(&rwmu), thrd_busy); // can't get read lock when write lock is held
  asserteq(rwmtx_unlock(&rwmu), thrd_success);
  asserteq(rwmtx_unlock(&rwmu), thrd_error); // no lock held

  rwmtx_destroy(&rwmu);
}


R_UNIT_TEST(rwmtx_threads) {
  rwmtx_t rwmu;
  rwmtx_init(&rwmu, mtx_plain);

  // spawn an even number of threads where every odd thread writes and every even thread reads
  TestThread threads[8] = {};
  atomic_u32 rcount = 0; // current number of writes
  atomic_u32 wcount = 0; // current number of writes
  atomic_u32 rcount_while_writing = 0; // value of rcount while writing
  atomic_u32 wcount_while_reading = 0; // value of wcount while writing

  // spawn threads
  for (u32 i = 0; i < countof(threads); i++) {
    TestThread* t = &threads[i];
    t->id = i;
    t->rwmu = &rwmu;
    t->rcount = &rcount;
    t->wcount = &wcount;
    t->rcount_while_writing = &rcount_while_writing;
    t->wcount_while_reading = &wcount_while_reading;
    assert(thrd_create(&t->t, test_thread, t) == thrd_success);
  }

  // wait for threads to finish
  for (u32 i = 0; i < countof(threads); i++) {
    auto t = &threads[i];
    int returnValue;
    thrd_join(t->t, &returnValue);
  }

  // counters should be balanced
  asserteq(AtomicLoadAcq(&rcount), 0);
  asserteq(AtomicLoadAcq(&wcount), 0);

  // there should have been no writing happening while reading
  asserteq(AtomicLoadAcq(&wcount_while_reading), 0);

  // there should have been no reading happening while writing
  asserteq(AtomicLoadAcq(&rcount_while_writing), 0);

  rwmtx_destroy(&rwmu);
}


#endif /* R_UNIT_TEST_ENABLED */
