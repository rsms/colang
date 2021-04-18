// example:
// #define HASHMAP_NAME     FooMap
// #define HASHMAP_KEY      Foo
// #define HASHMAP_KEY_HASH FooHash  // should return an unsigned integer
// #define HASHMAP_VALUE    char*
#ifndef HASHMAP_NAME
  #error "please define HASHMAP_NAME"
#endif
#ifndef HASHMAP_KEY
  #error "please define HASHMAP_KEY"
#endif
#ifndef HASHMAP_KEY_HASH
  #error "please define HASHMAP_KEY_HASH"
#endif
#ifndef HASHMAP_VALUE
  #error "please define HASHMAP_VALUE"
#endif

// entries per bucket
#ifndef HASHMAP_BUCKET_ENTRIES
#define HASHMAP_BUCKET_ENTRIES 8
#endif

#define _HM_MAKE_FN_NAME(a, b) a ## b
#define _HM_FUN(prefix, name) _HM_MAKE_FN_NAME(prefix, name)
#define HM_FUN(name) _HM_FUN(HASHMAP_NAME, name)

typedef enum HMFlag {
  HMFlagNone = 0,
  HMFlagBucketMemoryDense = 1 << 0,  // bucket memory is inside map memory. used by Free
} HMFlag;

typedef struct {
  struct {
    HASHMAP_KEY   key;
    HASHMAP_VALUE value;
  } entries[HASHMAP_BUCKET_ENTRIES];
} Bucket;


void HM_FUN(Init)(HASHMAP_NAME* m, u32 initbuckets, Mem mem) {
  m->cap = initbuckets;
  m->len = 0;
  m->flags = HMFlagNone;
  m->mem = mem;
  m->buckets = memalloc(mem, m->cap * sizeof(Bucket));
}

HASHMAP_NAME* HM_FUN(New)(u32 initbuckets, Mem mem) {
  // new differs from Init in that it allocates space for itself and the initial
  // buckets in one go. This is usually a little bit faster and reduces memory
  // fragmentation in cases where many hashmaps are created.
  size_t bucketSize = initbuckets * sizeof(Bucket);
  char* ptr = memalloc(mem, sizeof(HASHMAP_NAME) + bucketSize);
  auto m = (HASHMAP_NAME*)ptr;
  m->cap = initbuckets;
  m->mem = mem;
  m->flags = HMFlagBucketMemoryDense;
  if (bucketSize > 0)
    m->buckets = ptr + sizeof(HASHMAP_NAME);
  return m;
}

void HM_FUN(Dispose)(HASHMAP_NAME* m) {
  // should never call Dispose on a map created with New
  assert(!(m->flags & HMFlagBucketMemoryDense));

  memfree(m->mem, m->buckets);
  #if DEBUG
  m->buckets = NULL;
  m->len = 0;
  m->cap = 0;
  #endif
}

void HM_FUN(Free)(HASHMAP_NAME* m) {
  if (!(m->flags & HMFlagBucketMemoryDense)) {
    memfree(m->mem, m->buckets);
  }
  memfree(m->mem, m);
  #if DEBUG
  m->buckets = NULL;
  m->len = 0;
  m->cap = 0;
  #endif
}


static void mapGrow(HASHMAP_NAME* m) {
  u32 cap = m->cap * 2;
  rehash: {
    auto newbuckets = (Bucket*)memalloc(m->mem, cap * sizeof(Bucket));
    for (u32 bi = 0; bi < m->cap; bi++) {
      auto b = &((Bucket*)m->buckets)[bi];
      for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
        auto e = &b->entries[i];
        if (e->key == NULL) {
          break;
        }
        if (e->value == NULL) {
          // skip deleted entry (compactation)
          continue;
        }
        u32 index = ((u32)HASHMAP_KEY_HASH(e->key)) % cap;
        auto newb = &newbuckets[index];
        bool fit = false;
        for (u32 i2 = 0; i2 < HASHMAP_BUCKET_ENTRIES; i2++) {
          auto e2 = &newb->entries[i2];
          if (e2->key == NULL) {
            // found a free slot in newb
            *e2 = *e;
            fit = true;
            break;
          }
        }
        if (!fit) {
          // no free slot found in newb; need to grow further.
          memfree(m->mem, newbuckets);
          cap = cap * 2;
          goto rehash;
        }
      }
    }
    if (!(m->flags & HMFlagBucketMemoryDense)) {
      memfree(m->mem, m->buckets);
    }
    m->buckets = newbuckets;
    m->cap = cap;
    m->flags &= ~HMFlagBucketMemoryDense;
  }
}


// HM_FUN(Set) inserts key=value into m.
// Returns replaced value or NULL if key did not exist in map.
HASHMAP_VALUE HM_FUN(Set)(HASHMAP_NAME* m, HASHMAP_KEY key, HASHMAP_VALUE value) {
  assert(value != NULL);
  while (1) { // grow loop
    u32 index = ((u32)HASHMAP_KEY_HASH(key)) % m->cap;
    auto b = &((Bucket*)m->buckets)[index];
    // dlog("bucket(key=\"%s\") #%u  b=%p e=%p", key, index, b, &b->entries[0]);
    for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
      auto e = &b->entries[i];
      if (e->value == NULL) {
        // free slot
        e->key = key;
        e->value = value;
        m->len++;
        return NULL;
      }
      if (e->key == key) {
        // key already in map -- replace value
        auto oldval = e->value;
        e->value = value;
        return oldval;
      }
      // dlog("collision key=\"%s\" <> e->key=\"%s\"", key, e->key);
    }
    // overloaded -- grow buckets
    // dlog("grow & rehash");
    mapGrow(m);
  }
}


HASHMAP_VALUE HM_FUN(Del)(HASHMAP_NAME* m, HASHMAP_KEY key) {
  u32 index = ((u32)HASHMAP_KEY_HASH(key)) % m->cap;
  auto b = &((Bucket*)m->buckets)[index];
  for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
    auto e = &b->entries[i];
    if (e->key == key) {
      if (!e->value) {
        break;
      }
      // mark as deleted
      auto value = e->value;
      e->value = NULL;
      m->len--;
      return value;
    }
  }
  return NULL;
}


HASHMAP_VALUE HM_FUN(Get)(const HASHMAP_NAME* m, HASHMAP_KEY key) {
  u32 index = ((u32)HASHMAP_KEY_HASH(key)) % m->cap;
  auto b = &((Bucket*)m->buckets)[index];
  for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
    auto e = &b->entries[i];
    if (e->key == key) {
      return e->value;
    }
    if (e->key == NULL) {
      break;
    }
  }
  return NULL;
}


void HM_FUN(Clear)(HASHMAP_NAME* m) {
  memset(m->buckets, 0, sizeof(Bucket) * m->cap);
  m->len = 0;
}


void HM_FUN(Iter)(const HASHMAP_NAME* m, HM_FUN(Iterator)* it, void* userdata) {
  bool stop = false;
  for (u32 bi = 0; bi < m->cap; bi++) {
    auto b = &((Bucket*)m->buckets)[bi];
    for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
      auto e = &b->entries[i];
      if (e->key == NULL) {
        break;
      }
      if (e->value != NULL) {
        it(e->key, e->value, &stop, userdata);
        if (stop) {
          return;
        }
      }
    }
  }
}

// static u32* hashmapDebugDistr(const HASHMAP_NAME* m) {
//   u32 valindex = 0;
//   u32* vals = (u32*)memalloc(m->mem, m->cap * sizeof(u32));
//   for (u32 bi = 0; bi < m->cap; bi++) {
//     auto b = &((Bucket*)m->buckets)[bi];
//     u32 depth = 0;
//     for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
//       auto e = &b->entries[i];
//       if (e->key == NULL) {
//         break;
//       }
//       if (e->value != NULL) {
//         depth++;
//       }
//     }
//     vals[valindex++] = depth;
//   }
//   return vals;
// }

#undef _HM_MAKE_FN_NAME
#undef _HM_FUN
#undef HM_FUN
