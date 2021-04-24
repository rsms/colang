#pragma once
//
// Pool is a thread-safe "set" that can be used for free-lists.
//
// Entries must inherit PoolEntry or begin with POOL_ENTRY_HEAD
//
ASSUME_NONNULL_BEGIN

#define POOL_ENTRY_HEAD \
  _Atomic(void*) PoolNext; // initialize to NULL

typedef struct PoolEntry {
  _Atomic(struct PoolEntry*) PoolNext;
} PoolEntry;

struct _PoolHead {
  PoolEntry* ptr;
  uintptr_t  tag;
};

// Pool should be zero-initialized
typedef struct Pool {
  _Atomic(struct _PoolHead) head;
} Pool;

// PoolAdd adds e to fl
void PoolAdd(Pool* fl, PoolEntry* e);

// PoolTake attempts to retrieve a free entry from fl
PoolEntry* nullable PoolTake(Pool* fl);

// PoolHead is useful for traversing the list when there's no contention
// (e.g. to destroy remaining nodes)
PoolEntry* PoolHead(const Pool* fl);

ASSUME_NONNULL_END
