// Note: intentionally not "#pragma once"
ASSUME_NONNULL_BEGIN

// example:
// #define HASHMAP_NAME     FooMap
// #define HASHMAP_KEY      Foo
// #define HASHMAP_KEY_HASH FooHash  // only needed for HASHMAP_IMPLEMENTATION
// #define HASHMAP_VALUE    char*
#ifndef HASHMAP_NAME
  #error "please define HASHMAP_NAME"
#endif

// entries per bucket
#ifndef HASHMAP_BUCKET_ENTRIES
  #define HASHMAP_BUCKET_ENTRIES 8
#endif

#define _HM_CAT(a, b) a ## b
#define _HM(prefix, name)  _HM_CAT(prefix, name)
#define HM(name)           _HM(HASHMAP_NAME, name)

#define HM_BUCKET          HM(Bucket)
#define HM_BUCKET_ENTRY    HM(BucketEnt)


#if !defined(HASHMAP_IMPLEMENTATION)
// ======================================================================================
// interface

typedef struct {
  HASHMAP_KEY   key;
  HASHMAP_VALUE value;
} HM_BUCKET_ENTRY;

typedef struct {
  HM_BUCKET_ENTRY entries[HASHMAP_BUCKET_ENTRIES];
} HM_BUCKET;

// HASHMAP_NAME defines a hash map
typedef struct {
  u32        cap;     // number of buckets
  u32        len;     // number of key-value entries
  Mem        mem;     // memory allocator
  HM_BUCKET* buckets;
} HASHMAP_NAME;


#ifdef HASHMAP_INCLUDE_DECLARATIONS
// Prototype declarations:
// Normally these are copy-pasted and hand-converted in the user-level header
// for better documentation, but you can define HASHMAP_INCLUDE_DECLARATIONS to
// have these be auto-generated.

#ifndef HASHMAP_KEY
  #error "please define HASHMAP_KEY"
#endif
#ifndef HASHMAP_VALUE
  #error "please define HASHMAP_VALUE"
#endif

// Init initializes a map structure with user-provided initial bucket storage.
// initbucketsc*sizeof(HM_BUCKET) bytes must be available at initbucketsv and
// initbucketsv must immediately follow in memory, i.e. at m+sizeof(HASHMAP_NAME).
void HM(Init)(HASHMAP_NAME* m, void* initbucketsv, u32 initbucketsc, Mem mem);

// Dispose frees buckets data (but not the hashmap itself.)
// The hashmap is invalid after this call. Call Init to reuse.
void HM(Dispose)(HASHMAP_NAME*);


// New creates a new map with initbuckets number of initial buckets allocated in mem.
// Returns an error on allocation failure.
HASHMAP_NAME* nullable HM(New)(Mem mem, u32 initbuckets);

// Free frees all memory of a map, including the map's memory.
// ONLY USE Free with maps created with New.
// (Use Dispose when you manage the memory of the map yourself.)
void HM(Free)(HASHMAP_NAME*);


// Len returns the number of entries currently in the map
static u32 HM(Len)(const HASHMAP_NAME*);

// Get searches for key. Returns value, or NULL if not found.
HASHMAP_VALUE nullable HM(Get)(const HASHMAP_NAME*, HASHMAP_KEY key);

// Set inserts key=value into m.
// On return, sets *valuep_inout to a replaced value or NULL if no existing value was found.
// Returns an error if memory allocation failed during growth of the hash table.
error HM(Set)(HASHMAP_NAME*, HASHMAP_KEY key, HASHMAP_VALUE* valuep_inout);

// Del removes value for key. Returns the removed value or NULL if not found.
HASHMAP_VALUE nullable HM(Del)(HASHMAP_NAME*, HASHMAP_KEY key);

// Clear removes all entries. In contrast to Free, map remains valid.
void HM(Clear)(HASHMAP_NAME*);

// Iterator function type. Set stop=true to stop iteration.
typedef void(*HM(Iterator))(
  HASHMAP_KEY key, HASHMAP_VALUE value, bool* stop, void* userdata);

// Iter iterates over entries of the map.
void HM(Iter)(const HASHMAP_NAME*, HM(Iterator)*, void* userdata);


#endif // HASHMAP_INCLUDE_DECLARATIONS


inline static u32 HM(Len)(const HASHMAP_NAME* h) {
  return h->len;
}


// ======================================================================================
#else // HASHMAP_IMPLEMENTATION


#ifndef HASHMAP_KEY
  #error "please define HASHMAP_KEY"
#endif
#ifndef HASHMAP_KEY_HASH
  #error "please define HASHMAP_KEY_HASH"
#endif
#ifndef HASHMAP_VALUE
  #error "please define HASHMAP_VALUE"
#endif


void HM(Init)(HASHMAP_NAME* m, void* initbucketsv, u32 initbucketsc, Mem mem) {
  assert(initbucketsv == (void*)m + sizeof(HASHMAP_NAME));
  m->cap = initbucketsc;
  m->len = 0;
  m->mem = mem;
  m->buckets = initbucketsv;
}

HASHMAP_NAME* nullable HM(New)(Mem mem, u32 initbuckets) {
  // z = sizeof(HASHMAP_NAME) + sizeof(HM_BUCKET)*initbuckets
  usize z;
  if (check_mul_overflow((usize)initbuckets, sizeof(HM_BUCKET), &z))
    return NULL;
  if (check_add_overflow(z, sizeof(HASHMAP_NAME), &z))
    return NULL;
  HASHMAP_NAME* m = memalloc(mem, z);
  if (m)
    HM(Init)(m, (void*)m + sizeof(HASHMAP_NAME), initbuckets, mem);
  return m;
}

void HM(Dispose)(HASHMAP_NAME* m) {
  if (m->buckets != (void*)m + sizeof(HASHMAP_NAME))
    memfree(m->mem, m->buckets);
  #if DEBUG
  m->buckets = NULL;
  m->len = 0;
  m->cap = 0;
  #endif
}

void HM(Free)(HASHMAP_NAME* m) {
  HM(Dispose)(m);
  memfree(m->mem, m);
}

// hashmap_grow returns false if memory allocation failed
static bool hashmap_grow(HASHMAP_NAME* m) {
  usize cap = (usize)m->cap * 2;
  HM_BUCKET* newbuckets = NULL;
  usize z;
  // TODO: use realloc with m->buckets (need to revisit the growth loops)
rehash:
  z = array_size(sizeof(HM_BUCKET), cap);
  if (z == USIZE_MAX || (newbuckets = memrealloc(m->mem, newbuckets, z)) == NULL) {
    if (newbuckets)
      memfree(m->mem, newbuckets);
    return false;
  }

  for (u32 bi = 0; bi < m->cap; bi++) {
    HM_BUCKET* b = &((HM_BUCKET*)m->buckets)[bi];
    for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
      HM_BUCKET_ENTRY* e = &b->entries[i];
      if (e->key == NULL) {
        break;
      }
      if (e->value == NULL) {
        // skip deleted entry (compactation)
        continue;
      }
      usize index = ((usize)HASHMAP_KEY_HASH(e->key)) % cap;
      HM_BUCKET* newb = &newbuckets[index];
      bool fit = false;
      for (u32 i2 = 0; i2 < HASHMAP_BUCKET_ENTRIES; i2++) {
        HM_BUCKET_ENTRY* e2 = &newb->entries[i2];
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

  if (m->buckets != (void*)m + sizeof(HASHMAP_NAME))
    memfree(m->mem, m->buckets);

  m->buckets = newbuckets;
  m->cap = cap;
  return true;
}

error HM(Set)(HASHMAP_NAME* m, HASHMAP_KEY key, HASHMAP_VALUE* valuep_inout) {
  assert(*valuep_inout != NULL);
  while (1) { // grow loop
    usize index = ((usize)HASHMAP_KEY_HASH(key)) % m->cap;
    HM_BUCKET* b = &((HM_BUCKET*)m->buckets)[index];
    // dlog("bucket(key=\"%s\") #%u  b=%p e=%p", key, index, b, &b->entries[0]);
    for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
      HM_BUCKET_ENTRY* e = &b->entries[i];
      if (e->value == NULL) {
        // free slot
        e->key = key;
        e->value = *valuep_inout;
        m->len++;
        *valuep_inout = NULL;
        return 0;
      }
      if (e->key == key) {
        // key already in map -- replace value
        HASHMAP_VALUE oldval = e->value;
        e->value = *valuep_inout;
        *valuep_inout = oldval;
        return 0;
      }
      // dlog("collision key=\"%s\" <> e->key=\"%s\"", key, e->key);
    }
    // overloaded -- grow buckets
    // dlog("grow & rehash");
    if (!hashmap_grow(m)) {
      *valuep_inout = NULL;
      return err_nomem;
    }
  }
}

HASHMAP_VALUE HM(Del)(HASHMAP_NAME* m, HASHMAP_KEY key) {
  u32 index = ((u32)HASHMAP_KEY_HASH(key)) % m->cap;
  HM_BUCKET* b = &((HM_BUCKET*)m->buckets)[index];
  for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
    HM_BUCKET_ENTRY* e = &b->entries[i];
    if (e->key == key) {
      if (!e->value) {
        break;
      }
      // mark as deleted
      HASHMAP_VALUE value = e->value;
      e->value = NULL;
      m->len--;
      return value;
    }
  }
  return NULL;
}


HASHMAP_VALUE HM(Get)(const HASHMAP_NAME* m, HASHMAP_KEY key) {
  u32 index = ((u32)HASHMAP_KEY_HASH(key)) % m->cap;
  HM_BUCKET* b = &((HM_BUCKET*)m->buckets)[index];
  for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
    HM_BUCKET_ENTRY* e = &b->entries[i];
    if (e->key == key) {
      return e->value;
    }
    if (e->key == NULL) {
      break;
    }
  }
  return NULL;
}


void HM(Clear)(HASHMAP_NAME* m) {
  memset(m->buckets, 0, sizeof(HM_BUCKET) * m->cap);
  m->len = 0;
}


void HM(Iter)(const HASHMAP_NAME* m, HM(Iterator) it, void* nullable userdata) {
  bool stop = false;
  for (u32 bi = 0; bi < m->cap; bi++) {
    HM_BUCKET* b = &((HM_BUCKET*)m->buckets)[bi];
    for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
      HM_BUCKET_ENTRY* e = &b->entries[i];
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
//     HM_BUCKET* b = &((HM_BUCKET*)m->buckets)[bi];
//     u32 depth = 0;
//     for (u32 i = 0; i < HASHMAP_BUCKET_ENTRIES; i++) {
//       HM_BUCKET_ENTRY* e = &b->entries[i];
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


#endif

#undef _HM_CAT
#undef _HM
#undef HM
#undef HM_BUCKET
#undef HM_BUCKET_ENTRY

ASSUME_NONNULL_END
