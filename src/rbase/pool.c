#include "rbase.h"
#include "pool.h"

// Adapted from:
// https://moodycamel.com/blog/2014/solving-the-aba-problem-for-lock-free-free-lists
// https://blog.lse.epita.fr//2013/02/27/implementing-generic-double-word-compare-and-swap.html

// the following implementation relies on double-wide CAS
static_assert(__atomic_always_lock_free(sizeof(struct _PoolHead),0), "DCAS must be supported");

void PoolAdd(Pool* fl, PoolEntry* e) {
  struct _PoolHead currhead = atomic_load_explicit(&fl->head, memory_order_relaxed);
  struct _PoolHead nexthead = { e, 0 };
  do {
    nexthead.tag = currhead.tag + 1;
    atomic_store_explicit(&e->PoolNext, currhead.ptr, memory_order_relaxed);
  } while (
    !atomic_compare_exchange_weak_explicit(
      &fl->head, &currhead, nexthead, memory_order_release, memory_order_relaxed)
  );
}

PoolEntry* nullable PoolTake(Pool* fl) {
  struct _PoolHead currhead = atomic_load_explicit(&fl->head, memory_order_acquire);
  struct _PoolHead nexthead;
  while (currhead.ptr != NULL) {
    nexthead.ptr = atomic_load_explicit(&currhead.ptr->PoolNext, memory_order_relaxed);
    nexthead.tag = currhead.tag + 1;
    if (atomic_compare_exchange_weak_explicit(
          &fl->head, &currhead, nexthead, memory_order_release, memory_order_acquire))
    {
      break;
    }
  }
  return currhead.ptr;
}

// PoolHead is useful for traversing the list when there's no contention
// (e.g. to destroy remaining nodes)
PoolEntry* PoolHead(const Pool* fl) {
  struct _PoolHead currhead = atomic_load_explicit(&fl->head, memory_order_acquire);
  return currhead.ptr;
}

// -----------------------------------------------------------------------------------------------
#if R_UNIT_TEST_ENABLED

#include "thread.h"

typedef struct TestEntry {
  POOL_ENTRY_HEAD
  int value;
} TestEntry;

typedef struct TestThread {
  thrd_t      t;  // read-only
  u32         id; // read-only
  Pool*       fl; // read-only
  u32         entriesc; // in: entries to get. out: number of entries owned
  TestEntry** entriesv; // in: unused. out: entries owned
} TestThread;

static int test_thread(void* arg) {
  auto t = (TestThread*)arg;
  thrd_yield();
  //dlog("thread %u", t->id);

  for (u32 i = 0; i < t->entriesc; i++) {
    t->entriesv[i] = (TestEntry*)PoolTake(t->fl);
  }
  msleep(rand() % 10);
  // thrd_yield();
  for (u32 i = 0; i < t->entriesc; i++) {
    if (t->entriesv[i] != NULL)
      PoolAdd(t->fl, (PoolEntry*)t->entriesv[i]);
  }
  // thrd_yield();
  msleep((rand() % 10) + 1);
  u32 entriesc = 0;
  for (u32 i = 0; i < t->entriesc; i++) {
    auto e = (TestEntry*)PoolTake(t->fl);
    if (e != NULL)
      t->entriesv[entriesc++] = e;
  }
  t->entriesc = entriesc;
  //dlog("thread %u exiting", t->id);
  atomic_thread_fence(memory_order_release);
  return 0;
}

R_UNIT_TEST(Pool, {
  { // test basic functionality, without contention
    TestEntry e1 = { .value = 1 };
    TestEntry e2 = { .value = 2 };
    TestEntry e3 = { .value = 3 };
    TestEntry e4 = { .value = 4 };

    Pool fl = {};

    PoolAdd(&fl, (PoolEntry*)&e1);
    PoolAdd(&fl, (PoolEntry*)&e2);
    PoolAdd(&fl, (PoolEntry*)&e3);
    PoolAdd(&fl, (PoolEntry*)&e4);

    assert(PoolTake(&fl) == (PoolEntry*)&e4);
    assert(PoolTake(&fl) == (PoolEntry*)&e3);
    assert(PoolTake(&fl) == (PoolEntry*)&e2);
    assert(PoolTake(&fl) == (PoolEntry*)&e1);
  }

  { // test fuzzing with threads
    const u32 numthreads = 10;
    const u32 numentries = 10; // per thread
    Pool fl = {};
    TestThread threads[numthreads];
    TestEntry entries[numentries * numthreads];

    // create entries that will be shared amongst the threads
    size_t expectedTallyIdSum = 0;
    for (u32 i = 0; i < numentries * numthreads; i++) {
      entries[i].value = i + 1; // 1-based for tallyIdSum
      expectedTallyIdSum += i + 1;
      PoolAdd(&fl, (PoolEntry*)&entries[i]);
    }

    // dlog("spawning %u threads", numthreads);
    for (u32 i = 0; i < numthreads; i++) {
      TestThread* t = &threads[i];
      t->id = i;
      t->fl = &fl;
      t->entriesv = memalloc(NULL, sizeof(void*) * numentries);
      t->entriesc = numentries;
      assert(thrd_create(&t->t, test_thread, t) == thrd_success);
    }

    // dlog("awaiting %u threads", numthreads);
    TestEntry* tallyv[numentries * numthreads * 2];
    u32 tallyc = 0;
    size_t tallyIdSum = 0;
    for (u32 i = 0; i < numthreads; i++) {
      auto t = &threads[i];
      int returnValue;
      thrd_join(t->t, &returnValue);
      atomic_thread_fence(memory_order_acquire);
      // dlog("thread #%u returned %d", i, returnValue);
      // tally
      for (u32 i = 0; i < t->entriesc; i++) {
        auto e = t->entriesv[i];
        tallyv[tallyc++] = e;
        // verify the uniqueness of the values taken by summing up their ids as each
        // id is unique and part of a dense range.
        tallyIdSum += (size_t)e->value;
      }
    }

    // the same number of entries should be the total of entries taken by all threads
    assert(tallyc == numentries * numthreads);

    // each entry should only be referenced in one place.
    // if this fails, an entry was returned to two threads during contention.
    assert(tallyIdSum == expectedTallyIdSum);

    // free heap-allocated memory
    for (u32 i = 0; i < numthreads; i++) {
      memfree(NULL, threads[i].entriesv);
    }

    dlog("Pool thread test OK");
  }

  dlog("");
});

#endif /*R_UNIT_TEST_ENABLED*/
