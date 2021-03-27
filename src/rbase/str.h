#pragma once
ASSUME_NONNULL_BEGIN

// typedef struct Ref {
//   _Atomic u32 refs;
// } Ref;

// size_t count = atomic_fetch_add_explicit(&q->count, 1, memory_order_acquire);

// typedef struct Str {
//   Ref         _;
//   const u32   len;
//   const char  p[0];
// } Str;

// static const Str StrEmpty = {{1},0};

// Str* StrNewLen(Mem mem, const void* data, size_t z);

// static const char* CStr(Str* s);
// static size_t StrLen(Str* s);


// static inline const char* CStr(Str* s) { return s->p; }
// static inline size_t StrLen(Str* s) { return s->len; }

ASSUME_NONNULL_END
