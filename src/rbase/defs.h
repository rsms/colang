#pragma once
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/errno.h>
#include <sys/types.h>
#ifndef __cplusplus
  #include <stdatomic.h>
#endif

#include "target.h"

typedef signed char            i8;
typedef unsigned char          u8;
typedef signed short int       i16;
typedef unsigned short int     u16;
typedef signed int             i32;
typedef unsigned int           u32;
typedef signed long long int   i64;
typedef unsigned long long int u64;
typedef float                  f32;
typedef double                 f64;
typedef unsigned int           uint;
typedef unsigned long          size_t;
typedef signed long            ssize_t;

#ifndef __cplusplus
  typedef _Atomic(i8)      atomic_i8;
  typedef _Atomic(u8)      atomic_u8;
  typedef _Atomic(i16)     atomic_i16;
  typedef _Atomic(u16)     atomic_u16;
  typedef _Atomic(i32)     atomic_i32;
  typedef _Atomic(u32)     atomic_u32;
  typedef _Atomic(i64)     atomic_i64;
  typedef _Atomic(u64)     atomic_u64;
  typedef _Atomic(f32)     atomic_f32;
  typedef _Atomic(f64)     atomic_f64;
  typedef _Atomic(uint)    atomic_uint;
  typedef _Atomic(size_t)  atomic_size;
  typedef _Atomic(ssize_t) atomic_ssize;
#endif

#define auto          __auto_type
#define nullable      _Nullable
#define nonull        _Nonnull
#define nonnullreturn __attribute__((returns_nonnull))

#ifndef __cplusplus
  #define auto        __auto_type
  #define noreturn    _Noreturn

  #if __has_c_attribute(fallthrough)
    #define FALLTHROUGH [[fallthrough]]
  #else
    #define FALLTHROUGH
  #endif
#endif

#ifndef thread_local
  #define thread_local _Thread_local
#endif

#define ASSUME_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#define ASSUME_NONNULL_END   _Pragma("clang assume_nonnull end")

#ifdef __cplusplus
  #define EXTERN_C extern "C"
#else
  #define EXTERN_C
#endif

// define WIN32 if target is MS Windows
#ifndef WIN32
#  ifdef _WIN32
#    define WIN32 1
#  endif
#  ifdef _WIN32_WCE
#    define LACKS_FCNTL_H
#    define WIN32 1
#  endif
#endif

// ATTR_FORMAT(archetype, string-index, first-to-check)
// archetype determines how the format string is interpreted, and should be printf, scanf,
// strftime or strfmon.
// string-index specifies which argument is the format string argument (starting from 1),
// while first-to-check is the number of the first argument to check against the format string.
// For functions where the arguments are not available to be checked (such as vprintf),
// specify the third parameter as zero.
#if __has_attribute(format)
  #define ATTR_FORMAT(...) __attribute__((format(__VA_ARGS__)))
#else
  #define ATTR_FORMAT(...)
#endif

#if __has_attribute(always_inline)
  #define ALWAYS_INLINE __attribute__((always_inline))
#else
  #define ALWAYS_INLINE
#endif

#if __has_attribute(noinline)
  #define NO_INLINE __attribute__((noinline))
#else
  #define NO_INLINE
#endif

// _errlog is implemented in util.c
void _errlog(const char* fmt, ...);

#ifdef DEBUG
  #define dlog(fmt, ...) \
    fprintf(stderr, "\e[1;36m%-15s\e[39m " fmt "\e[0m\n", __FUNCTION__, ##__VA_ARGS__)
  #define errlog(fmt, ...) _errlog(fmt " (%s:%d)", ##__VA_ARGS__, __FILE__, __LINE__)
#else
  #define dlog(...)  do{}while(0)
  #define errlog _errlog
#endif

#define TODO_IMPL ({ \
  _errlog("\e[1;33mTODO_IMPL %s\e[0m  %s:%d", __PRETTY_FUNCTION__, __FILE__, __LINE__); \
  abort(); \
})

#define UNREACHABLE ({ \
  _errlog("UNREACHABLE %s %s:%d", __PRETTY_FUNCTION__, __FILE__, __LINE__); \
  abort(); \
})

#ifndef offsetof
  #define offsetof(st, m) ((size_t)&(((st*)0)->m))
#endif

#define countof(x) \
  ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#define MAX(a,b) \
  ({__typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

#define MIN(a,b) \
  ({__typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a < _b ? _a : _b; })

// division of integer, rounding up
#define r_idiv_ceil(x, y) (1 + (((x) - 1) / (y)))

// r_type_formatter yields a printf formatting pattern for the type of x
#define r_type_formatter(x) _Generic((x), \
  unsigned long long: "%llu", \
  unsigned long:      "%lu", \
  unsigned int:       "%u", \
  long long:          "%lld", \
  long:               "%ld", \
  int:                "%d", \
  char:               "%c", \
  unsigned char:      "%C", \
  const char*:        "%s", \
  char*:              "%s", \
  void*:              "%p", \
  const void*:        "%p", \
  default:            "%p" \
)

// len retrieves the length of x
#define len(x) _Generic((x), \
  const char*:        strlen, \
  char*:              strlen \
)(x)

// T align2<T>(T x, T y) rounds up n to closest boundary w (w must be a power of two)
//
// E.g.
//   align(0, 4) => 0
//   align(1, 4) => 4
//   align(2, 4) => 4
//   align(3, 4) => 4
//   align(4, 4) => 4
//   align(5, 4) => 8
//   ...
//
#define align2(n,w) ({ \
  assert(((w) & ((w) - 1)) == 0); /* alignment w is not a power of two */ \
  ((n) + ((w) - 1)) & ~((w) - 1); \
})

// UNCONST_TYPEOF(v) yields __typeof__ without const qualifier (for basic types only)
#define UNCONST_TYPEOF(x)                                     \
  __typeof__(_Generic((x),                                    \
    signed char:              ({ signed char        _; _; }), \
    const signed char:        ({ signed char        _; _; }), \
    unsigned char:            ({ unsigned char      _; _; }), \
    const unsigned char:      ({ unsigned char      _; _; }), \
    short:                    ({ short              _; _; }), \
    const short:              ({ short              _; _; }), \
    unsigned short:           ({ unsigned short     _; _; }), \
    const unsigned short:     ({ unsigned short     _; _; }), \
    int:                      ({ int                _; _; }), \
    const int:                ({ int                _; _; }), \
    unsigned:                 ({ unsigned           _; _; }), \
    const unsigned:           ({ unsigned           _; _; }), \
    long:                     ({ long               _; _; }), \
    const long:               ({ long               _; _; }), \
    unsigned long:            ({ unsigned long      _; _; }), \
    const unsigned long:      ({ unsigned long      _; _; }), \
    long long:                ({ long long          _; _; }), \
    const long long:          ({ long long          _; _; }), \
    unsigned long long:       ({ unsigned long long _; _; }), \
    const unsigned long long: ({ unsigned long long _; _; }), \
    float:                    ({ float              _; _; }), \
    const float:              ({ float              _; _; }), \
    double:                   ({ double             _; _; }), \
    const double:             ({ double             _; _; }), \
    long double:              ({ long double        _; _; }), \
    const long double:        ({ long double        _; _; }), \
    default: x \
  ))

// POW2_CEIL rounds up to power-of-two (from go runtime/stack)
#define POW2_CEIL(v) ({ \
  UNCONST_TYPEOF(v) x = v;  \
  x -= 1;               \
  x = x | (x >> 1);     \
  x = x | (x >> 2);     \
  x = x | (x >> 4);     \
  x = x | (x >> 8);     \
  x = x | (x >> 16);    \
  x + 1; })

// Some common atomic helpers
// See https://en.cppreference.com/w/c/atomic
// See https://en.cppreference.com/w/c/atomic/memory_order

#define AtomicLoad(x)        atomic_load_explicit((x), memory_order_relaxed)
#define AtomicLoadAcq(x)     atomic_load_explicit((x), memory_order_acquire)
#define AtomicStore(x, v)    atomic_store_explicit((x), (v), memory_order_relaxed)
#define AtomicStoreRel(x, v) atomic_store_explicit((x), (v), memory_order_release)

// note: these operations return the old value
#define AtomicAdd(x, n)      atomic_fetch_add_explicit((x), (n), memory_order_relaxed)
#define AtomicSub(x, n)      atomic_fetch_sub_explicit((x), (n), memory_order_relaxed)
#define AtomicOr(x, n)       atomic_fetch_or_explicit((x), (n), memory_order_relaxed)
#define AtomicAnd(x, n)      atomic_fetch_and_explicit((x), (n), memory_order_relaxed)
#define AtomicXor(x, n)      atomic_fetch_xor_explicit((x), (n), memory_order_relaxed)

#define AtomicCAS(p, oldval, newval) \
  atomic_compare_exchange_strong_explicit( \
    (p), (oldval), (newval), memory_order_acq_rel, memory_order_release)

#define AtomicCASRel(p, oldval, newval) \
  atomic_compare_exchange_strong_explicit( \
    (p), (oldval), (newval), memory_order_release, memory_order_release)

#define AtomicCASAcqRel(p, oldval, newval) \
  atomic_compare_exchange_strong_explicit( \
    (p), (oldval), (newval), memory_order_acq_rel, memory_order_release)
