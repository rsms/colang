#pragma once
ASSUME_NONNULL_BEGIN

typedef struct Hamt Hamt;
typedef struct HamtCtx HamtCtx;

// Hamt is an immutable persistent collection
typedef struct Hamt {
  void*    p;   // underlying handle
  HamtCtx* ctx; // user context
} Hamt;

// HamtCtx provides functionality contextual to the type of entries of a Hamt.
//
// To store arbitrary information alongside a Hamt, make your own struct e.g.
//   struct MyCtx { HamtCtx head; int mydata; };
//   hamt_new((HamtCtx*)&myctx);
// You can now access mydata in your callback functions by casting the ctx argument, e.g.
//   void myentfree(HamtCtx* ctx, void* entry) {
//     struct MyCtx* my = (struct MyCtx*)ctx;
//     ...
//   }
typedef struct HamtCtx {
  // entkey should return the entry's unique key. For example a hash or index.
  // The result of calling this function is cached for entries in a Hamt, so this
  // is only called once during a lookup, insertion or removal operation.
  // This callback is required and must not be NULL.
  u32(*entkey)(HamtCtx* ctx, const void* entry);

  // enteq should return true if a and b are considered equivalent.
  // This function is only called for entries with identical key() values.
  // It's used during lookup and removal to verify that the entry requested
  // indeed is what was found for a given key.
  // It's used during insertion to determine if an existing entry should be
  // replaced (eq==true) or not.
  // This callback is required and must not be NULL.
  bool(*enteq)(HamtCtx* ctx, const void* entry1, const void* entry2);

  // entfree is called when an entry is no longer referenced by any Hamt.
  // If entries are heap allocated, this would be the time to free them.
  // This callback is required and must not be NULL.
  void(*entfree)(HamtCtx* ctx, void* entry);

  // entreplace is called when an entry is replaced during insertion.
  // preventry is the entry being replaced and nextentry is the "new" entry taking its place.
  // Both entries have identical entkey values and are equivalent according to enteq.
  // This can be used to update bookeeping like total entry count.
  // This callback is optional and may be NULL.
  void(*entreplace)(HamtCtx* ctx, void* preventry, void* nextentry);

  // entrepr is called by hamt_repr to append a human-readable representation to a string.
  // This callback is optional and may be NULL.
  Str(*entrepr)(Str s, const void* entry);
} HamtCtx;

// hamt_new creates an empty Hamt with a +1 reference count
Hamt hamt_new(HamtCtx* ctx);
static Hamt hamt_retain(Hamt);  // increment reference count (returns input as a convenience)
static void hamt_release(Hamt); // decrement reference count

// hamt_with returns a version of h with entry added. Steals a ref to entry.
// Returns a new Hamt with a +1 reference count.
Hamt hamt_with(Hamt h, void* entry);

// hamt_set is a convenience form of hamt_with that does the following:
// 1. h2 = hamt_with(*h, entry)
// 2. hamt_release(*h)
// 3. *h = h2
// Steals a ref to entry.
void hamt_set(Hamt* h, void* entry);

// hamt_get performs a lookup of a entry equivalent to reference entry.
// If an entry is found, its value is stored to *entry and true is returned.
// The input value of *entry is used as the "lookup" value; it is queried for its key
// with entkey and may be compared via enteq with entries that has identical keys.
static bool hamt_get(Hamt h, const void** entry);

// hamt_getk is a variant of hamt_get where the lookup key is provided separately
// instead of being looked up via entkey. Note that the reference entry (input value of entry)
// is still queried with enteq.
bool hamt_getk(Hamt h, const void** entry, u32 key);

// hamt_getp is a convenience variant of hamt_get which returns NULL on failure.
// Only suitable when the entry type is a pointer or is never zero.
static const void* nullable hamt_getp(Hamt h, const void* refentry);

// map_repr appends a human-readable string representation of h to s.
// If h.ctx->entrepr is non-NULL, it will be used to format entries in the output.
Str hamt_repr(Hamt h, Str s);


// -----------------------------------------------------------------------------------------------
// implementation

void _hamt_free(Hamt);

inline static Hamt hamt_retain(Hamt h) {
  AtomicAdd((atomic_u32*)h.p, 1);
  return h;
}

inline static void hamt_release(Hamt h) {
  if (AtomicSub((atomic_u32*)h.p, 1) == 1)
    _hamt_free(h);
}

inline static const void* hamt_getp(Hamt h, const void* entry) {
  if (!hamt_get(h, &entry))
    return NULL;
  return entry;
}

inline static bool hamt_get(Hamt h, const void** entry) {
  return hamt_getk(h, entry, h.ctx->entkey(h.ctx, *entry));
}

ASSUME_NONNULL_END
