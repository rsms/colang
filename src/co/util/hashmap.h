// Note: intentionally not "#pragma once"
ASSUME_NONNULL_BEGIN

// example:
// #define HASHMAP_NAME     FooMap
// #define HASHMAP_KEY      Foo
// #define HASHMAP_VALUE    char*
#ifndef HASHMAP_NAME
  #error "please define HASHMAP_NAME"
#endif

// HASHMAP_NAME defines a hash map
typedef struct {
  u32   cap;     // number of buckets
  u32   len;     // number of key-value entries
  u32   flags;   // internal
  Mem   mem;     // memory allocator
  void* buckets; // internal
} HASHMAP_NAME;


#define _HM_MAKE_FN_NAME(a, b) a ## b
#define _HM_FUN(prefix, name) _HM_MAKE_FN_NAME(prefix, name)
#define HM_FUN(name) _HM_FUN(HASHMAP_NAME, name)


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


// New creates a new map with initbuckets intial buckets.
HASHMAP_NAME* HM_FUN(New)(u32 initbuckets, Mem)

// Free frees all memory of a map, including the map's memory.
// Use Free when you created a map with New.
// Use Dispose when you manage the memory of the map yourself and used Init.
void HM_FUN(Free)(HASHMAP_NAME*);

// Init initializes a map structure. initbuckets is the number of initial buckets.
void HM_FUN(Init)(HASHMAP_NAME*, u32 initbuckets, Mem mem);

// Dispose frees buckets data (but not the hashmap itself.)
// The hashmap is invalid after this call. Call Init to reuse.
void HM_FUN(Dispose)(HASHMAP_NAME*);

// Len returns the number of entries currently in the map
static u32 HM_FUN(Len)(const HASHMAP_NAME*);

// Get searches for key. Returns value, or NULL if not found.
HASHMAP_VALUE nullable HM_FUN(Get)(const HASHMAP_NAME*, HASHMAP_KEY key);

// Set inserts key=value into m. Returns the replaced value or NULL if not found.
HASHMAP_VALUE nullable HM_FUN(Set)(HASHMAP_NAME*, HASHMAP_KEY key, HASHMAP_VALUE value);

// Del removes value for key. Returns the removed value or NULL if not found.
HASHMAP_VALUE nullable HM_FUN(Del)(HASHMAP_NAME*, HASHMAP_KEY key);

// Clear removes all entries. In contrast to Free, map remains valid.
void HM_FUN(Clear)(HASHMAP_NAME*);

// Iterator function type. Set stop=true to stop iteration.
typedef void(*HM_FUN(Iterator))(HASHMAP_KEY key, HASHMAP_VALUE value, bool* stop, void* userdata);

// Iter iterates over entries of the map.
void HM_FUN(Iter)(const HASHMAP_NAME*, HM_FUN(Iterator)*, void* userdata);


#undef _HM_MAKE_FN_NAME
#undef _HM_FUN
#undef HM_FUN

#endif /* HASHMAP_INCLUDE_DECLARATIONS */


inline static u32 HM_FUN(Len)(const HASHMAP_NAME* h) {
  return h->len;
}


ASSUME_NONNULL_END
