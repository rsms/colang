#include "coimpl.h"
#include "test.h"
#include "mem.h"

void* mem_dup2(Mem mem, const void* src, usize len, usize extraspace) {
  assert(mem != NULL);
  assert(src != NULL);
  void* dst = mem->alloc(mem, len + extraspace);
  if (!dst)
    return NULL;
  return memcpy(dst, src, len);
}

char* mem_strdup(Mem mem, const char* cstr) {
  assert(cstr != NULL);
  usize z = strlen(cstr);
  char* s = (char*)mem_dup2(mem, cstr, z, 1);
  if (s)
    s[z] = 0;
  return s;
}

// --------------------------------------------------------------------------------------
// FixBufAllocator

typedef struct AllocHead AllocHead;

#define FBA_ALIGN sizeof(void*)
#define FBA_H2PTR(h) ( (void*)(h) + sizeof(AllocHead) )
#define FBA_PTR2H(p) ( (AllocHead*)((void*)(p) - sizeof(AllocHead)) )

// FixBufAllocator.flags
#define FBA_NEEDZERO (1 << 0) // allocz needs to do memset(p,0,size)
#define FBA_MUTATING (1 << 1) // mutation marker, for thread race detection [safe]

struct AllocHead {
  AllocHead* nullable next;
  usize               size;
  // followed by data here
};

static_assert(kFixBufAllocatorOverhead == sizeof(AllocHead), "keep in sync");
static_assert(alignof(AllocHead) <= FBA_ALIGN, "");


inline static usize fba_allocsize(usize size) {
  safecheckf(
    size < USIZE_MAX - kFixBufAllocatorOverhead &&
    ALIGN2(size, FBA_ALIGN) <= USIZE_MAX - kFixBufAllocatorOverhead,
    "size %zu too large", size);
  return ALIGN2(size + kFixBufAllocatorOverhead, FBA_ALIGN);
}


#ifdef CO_TESTING_ENABLED
  UNUSED static void fba_dump_free(FixBufAllocator* a) {
    if (a->free == NULL) {
      log(".free=NULL");
      return;
    }
    log(".free=");
    usize i = 0;
    for (AllocHead* h = a->free; h != NULL; h = h->next) {
      log("  block #%zu %p .. %p (%zu B) p %p",
        i, h, (void*)h + h->size, h->size, FBA_H2PTR(h));
      i++;
    }
  }

  // fba_list_test returns true if h is in list head
  static bool fba_list_test(AllocHead* head, AllocHead* h) {
    for (; head != NULL; head = head->next) {
      if (head == h)
        return true;
    }
    return false;
  }
#endif


static bool fba_istail(FixBufAllocator* a, AllocHead* h) {
  return a->buf + a->len - h->size == h;
}


static void fba_list_add_sort(void** head, AllocHead* entry) {
  AllocHead* loh = *head;
  if (loh == NULL) {
    entry->next = *head;
    *head = entry;
    return;
  }
  // find the block that is "max lower" than entry
  while (loh->next != NULL) {
    if (loh->next > entry)
      break;
    loh = loh->next;
  }
  // add entry after loh
  entry->next = loh->next;
  loh->next = entry;
}


inline static void fba_list_rm(void** head, AllocHead* entry, AllocHead* nullable prev) {
  if (prev == NULL) {
    *head = entry->next;
    return;
  }
  prev->next = entry->next;
}


// fba_find_prevuse finds the block closer to a->free head than refh.
// i.e. fba_find_prevuse(a,refh)->next == refh
static AllocHead* nullable fba_find_prevuse(FixBufAllocator* a, AllocHead* refh) {
  AllocHead* prev = NULL;
  for (AllocHead* h = a->use; h != NULL; prev = h, h = h->next) {
    if (h == refh)
      return prev;
  }
  safecheckf(0,"invalid address %p not in allocator %p", FBA_H2PTR(refh), a);
  UNREACHABLE;
}


// fba_find_free attempts to find and dequeue a block from a->free large enough for
// minsize. It may merge adjacent blocks.
// a->free is sorted from smallest to largest blocks.
static AllocHead* nullable fba_find_free(FixBufAllocator* a, usize minsize, bool lazy) {
  // TODO: consider binary search, since a->free is sorted (from smallest to largest.)
  // We would need to track the a->free list tail in addition to the head.

  // maxsize -- single blocks larger than this are only used as a last resort
  const usize maxsize = minsize < ISIZE_MAX/2 ? minsize*2 : minsize;

  // scanlimit limits the number of blocks we scan before giving up.
  // lazy=true when the caller is optimistically looking for a free block but is
  // otherwise able to allocate a new block.
  u32 scanlimit = lazy ? 10 : U32_MAX;

  // span_* tracks the current contiguous span of blocks
  usize      span_size = 0;
  AllocHead* span_start = a->free;
  AllocHead* span_prev = NULL;

  AllocHead* big = NULL;  // large block (h->size > maxsize)
  AllocHead* prev = NULL; // previous block in list

  for (AllocHead* h = a->free; h != NULL && --scanlimit; prev = h, h = h->next) {
    if (h->size >= minsize) {
      // block is large enough, but is it too large..? Minimize fragmentation.
      big = h;
      if (h->size <= maxsize) {
        // found a block of appropriate size
        //log("  found block %p .. %p (%zu B)", h, (void*)h + h->size, h->size);
        fba_list_rm(&a->free, h, prev);
        return h;
      }
    }

    // if blocks h and prev are not adjacent in memory, start a new span
    if (prev == NULL || (void*)prev + prev->size != h) {
      span_size = h->size;
      span_start = h;
      span_prev = prev;
      continue;
    }

    // increase span
    span_size += h->size;
    if (span_size < minsize)
      continue; // span is not large enough

    // if the first block of span is big, use that to minimize merges
    if (span_start->size > minsize) {
      fba_list_rm(&a->free, span_start, span_prev);
      // log("  found block %p .. %p (%zu B)",
      //   span_start, (void*)span_start + span_start->size, span_start->size);
      return span_start;
    }

    // remove span from free list
    if (span_prev == NULL) {
      a->free = h->next;
    } else {
      span_prev->next = h->next;
    }

    // merge blocks into one
    span_start->size = span_size;

    // log("  found span %p .. %p (%zu B)",
    //   span_start, (void*)span_start + span_start->size, span_start->size);
    return span_start;
  }

  if (lazy)
    return NULL; // don't use big blocks; let caller allocate new small block
  return big;
}


// fba_excl_begin and fba_excl_end check for thread races
inline static void fba_excl_begin(FixBufAllocator* a) {
  #ifdef CO_SAFE
  safecheckf((a->flags & FBA_MUTATING) == 0, "concurrent allocator mutation");
  a->flags ^= FBA_MUTATING;
  #endif
}

inline static void fba_excl_end(FixBufAllocator* a) {
  #ifdef CO_SAFE
  safecheckf((a->flags & FBA_MUTATING) != 0, "concurrent allocator mutation");
  a->flags &= ~FBA_MUTATING;
  #endif
}


static void* nullable fba_alloc1(FixBufAllocator* a, usize size) {
  assert(size > 0);
  assert(IS_ALIGN2(size, FBA_ALIGN));

  bool has_space = a->cap - a->len >= size;

  AllocHead* h = fba_find_free(a, size, has_space);
  if (h == NULL) {
    // allocate new block
    if UNLIKELY(!has_space)
      return NULL;
    h = a->buf + a->len;
    h->size = size;
    h->next = NULL;
    a->len += size;
  }

  // add to in-use list
  h->next = a->use;
  a->use = h;

  return FBA_H2PTR(h);
}


static void* nullable fba_alloc(Mem m, usize size) {
  FixBufAllocator* a = (FixBufAllocator*)m;
  size = fba_allocsize(size);
  fba_excl_begin(a);
  void* p = fba_alloc1(a, size);
  fba_excl_end(a);
  return p;
}


static void* nullable fba_allocz(Mem m, usize size) {
  FixBufAllocator* a = (FixBufAllocator*)m;
  fba_excl_begin(a);
  void* p = fba_alloc1(a, fba_allocsize(size));
  fba_excl_end(a);
  if (p != NULL && (a->flags & FBA_NEEDZERO))
    memset(p, 0, size);
  return p;
}


static void* nullable fba_resize(Mem m, void* nullable p, usize newsize) {
  if (p == NULL)
    return fba_alloc(m, newsize);

  newsize = fba_allocsize(newsize);
  FixBufAllocator* a = (FixBufAllocator*)m;
  fba_excl_begin(a);

  AllocHead* h = FBA_PTR2H(p);

  // we're done if there's already enough space in the block
  if UNLIKELY(newsize <= h->size)
    goto end;

  // grow
  usize addlsize = newsize - h->size;
  if UNLIKELY(a->cap - a->len < addlsize) {
    // not enough space
    p = NULL;
    goto end;
  }

  // at the tail of a->buf we can simply extend the block
  if (fba_istail(a, h)) {
    h->size = newsize;
    a->len += addlsize;
    goto end;
  }

  // relocate to new block
  void* newp = fba_alloc1(a, newsize);
  if UNLIKELY(newp == NULL) {
    p = newp;
    goto end;
  }
  memcpy(newp, p, h->size - sizeof(AllocHead));

  // free old block
  AllocHead* prevh = fba_find_prevuse(a, h);
  fba_list_rm(&a->use, h, prevh);
  fba_list_add_sort(&a->free, h);
  a->flags |= FBA_NEEDZERO;

  p = newp;
end:
  fba_excl_end(a);
  return p;
}


static void fba_free(Mem m, void* nonull p) {
  FixBufAllocator* a = (FixBufAllocator*)m;
  safenotnull(p);
  fba_excl_begin(a);
  a->flags |= FBA_NEEDZERO;

  AllocHead* h = FBA_PTR2H(p);

  if (fba_istail(a, h)) {
    // tail subtract optimization
    asserteq(h, a->use);
    a->use = h->next;
    a->len -= h->size;
    fba_excl_end(a);
    return;
  }

  AllocHead* prevh = fba_find_prevuse(a, h);
  fba_list_rm(&a->use, h, prevh);
  fba_list_add_sort(&a->free, h);
  fba_excl_end(a);
}


Mem FixBufAllocatorInitz(FixBufAllocator* a, void* buf, usize size) {
  assertf(
    size >= 8 ? *(u64*)buf == 0 :
    size >= 4 ? *(u32*)buf == 0 :
    size >= 2 ? *(u16*)buf == 0 :
    size >= 1 ? *(u8*)buf == 0 :
    true, "buf not zeroed");
  a->a.alloc = fba_alloc;
  a->a.allocz = fba_allocz;
  a->a.resize = fba_resize;
  a->a.free = fba_free;
  a->buf = (void*)ALIGN2((uintptr)buf, FBA_ALIGN);
  a->cap = (usize)(uintptr)((buf + size) - a->buf);
  a->len = 0;
  a->use = NULL;
  a->free = NULL;
  a->flags = 0;
  return (Mem)a;
}


Mem FixBufAllocatorInit(FixBufAllocator* a, void* buf, usize size) {
  memset(buf, 0, size);
  return FixBufAllocatorInitz(a, buf, size);
}


// --------------------------------------------------------------------------------------
#ifdef CO_TESTING_ENABLED

DEF_TEST(mem_fba) {
  u8 buf[256];
  FixBufAllocator ma;

  { // ideal allocation and deallocation pattern
    Mem mem = FixBufAllocatorInit(&ma, buf, sizeof(buf));
    void* a = memalloc(mem, 8);
    void* b = memalloc(mem, 8);
    void* c = memalloc(mem, 8);
    void* d = memalloc(mem, 8);
    void* e = memalloc(mem, 8);
    asserteq(ma.len, fba_allocsize(8)*5);
    // all of these should use the tail subtract optimization
    memfree(mem, e);
    asserteq(ma.len, fba_allocsize(8)*4);
    memfree(mem, d);
    asserteq(ma.len, fba_allocsize(8)*3);
    memfree(mem, c);
    asserteq(ma.len, fba_allocsize(8)*2);
    memfree(mem, b);
    asserteq(ma.len, fba_allocsize(8));
    memfree(mem, a);
    asserteq(ma.len, 0);
  }

  {
    // resize tail
    Mem mem = FixBufAllocatorInit(&ma, buf, sizeof(buf));
    void* a = memalloc(mem, 8);
    void* b = memalloc(mem, 8);
    void* c = memalloc(mem, 8);
    void* c2 = fba_resize(mem, c, 32);
    // block at tail should have grown rather than be relocated
    asserteq(c, c2);
    // allocation utilization should have grown
    asserteq(ma.len, fba_allocsize(8)*2 + fba_allocsize(32));
    // to contrast the above, resizing a should cause relocation
    void* a2 = fba_resize(mem, a, 32);
    assert(a != a2);
    // allocation utilization should have grown
    asserteq(ma.len, fba_allocsize(8)*2 + fba_allocsize(32)*2);

    memfree(mem, a2);
    memfree(mem, b);
    memfree(mem, c2);
  }

  { // free shuffle
    Mem mem = FixBufAllocatorInit(&ma, buf, sizeof(buf));

    void* a = memalloc(mem, 8);
    void* b = memalloc(mem, 8);
    void* c = memalloc(mem, 8);
    void* d = memalloc(mem, 8);
    void* e = memalloc(mem, 8);

    void* bufexpect = buf;
    asserteq(bufexpect, FBA_PTR2H(a)); // at buf[0]
    bufexpect += fba_allocsize(8);
    asserteq(bufexpect, FBA_PTR2H(b));
    bufexpect += fba_allocsize(8);
    asserteq(bufexpect, FBA_PTR2H(c));
    bufexpect += fba_allocsize(8);
    asserteq(bufexpect, FBA_PTR2H(d));
    bufexpect += fba_allocsize(8);
    asserteq(bufexpect, FBA_PTR2H(e));

    memfree(mem, b);
    memfree(mem, d);
    memfree(mem, c);

    void* bc = memalloc(mem, 16);
    assertnotnull(bc);
    void* bc2 = fba_resize(mem, bc, 32);
    asserteq(bc, bc2); // should be enough space in bc block
    memfree(mem, bc);
    // bc should be b & c blocks reused and merged into one
    asserteq(bc, b);

    // fba_dump_free(&ma);
    memfree(mem, a);
    memfree(mem, e);
    // fba_dump_free(&ma);
  }

  { // run out of buffer on memalloc
    Mem mem = FixBufAllocatorInit(&ma, buf, sizeof(buf));
    void* a = memalloc(mem, sizeof(buf)/2);
    void* b = memalloc(mem, sizeof(buf)/2);
    assertnotnull(a);
    assertnull(b);
  }

  { // run out of buffer on memresize
    Mem mem = FixBufAllocatorInit(&ma, buf, sizeof(buf));
    void* a = memalloc(mem, sizeof(buf)/2);
    void* b = memresize(mem, a, sizeof(buf));
    assertnotnull(a);
    assertnull(b); // should fail
    // a should still be valid, still be in use
    assert(fba_list_test(ma.use, FBA_PTR2H(a)));
  }
}

#endif // CO_TESTING_ENABLED
