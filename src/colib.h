// co common library
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#ifndef CO_LIB
#define CO_LIB
#include "colib.h"

#ifndef __cplusplus
  typedef _Bool bool;
  #define true  ((bool)1)
  #define false ((bool)0)
#endif
#ifdef __INT8_TYPE__
  typedef __INT8_TYPE__       i8;
  typedef __UINT8_TYPE__      u8;
#else
  typedef signed char         i8;
  typedef unsigned char       u8;
#endif
#ifdef __INT16_TYPE__
  typedef __INT16_TYPE__      i16;
  typedef __UINT16_TYPE__     u16;
#else
  typedef signed short        i16;
  typedef unsigned short      u16;
#endif
#ifdef __INT32_TYPE__
  typedef __INT32_TYPE__      i32;
  typedef __UINT32_TYPE__     u32;
#else
  typedef signed int          i32;
  typedef unsigned int        u32;
#endif
#ifdef __INT64_TYPE__
  typedef __INT64_TYPE__      i64;
  typedef __UINT64_TYPE__     u64;
#else
  typedef signed long long    i64;
  typedef unsigned long long  u64;
#endif
#ifdef __SIZE_TYPE__
  typedef __SIZE_TYPE__       usize;
#else
  typedef unsigned long       usize;
#endif
#ifdef __INTPTR_TYPE__
  typedef __INTPTR_TYPE__     intptr;
  typedef __UINTPTR_TYPE__    uintptr;
#else
  typedef signed long         intptr;
  typedef unsigned long       uintptr;
#endif
typedef float  f32;
typedef double f64;

#define I8_MAX    0x7f
#define I16_MAX   0x7fff
#define I32_MAX   0x7fffffff
#define I64_MAX   0x7fffffffffffffffLL
#ifdef __SIZE_MAX__
  #define ISIZE_MAX (__SIZE_MAX__ >> 1)
#else
  #define ISIZE_MAX __LONG_MAX__
#endif

#define I8_MIN    (-1-0x7f)
#define I16_MIN   (-1-0x7fff)
#define I32_MIN   (-1-0x7fffffff)
#define I64_MIN   (-1-0x7fffffffffffffff)
#define ISIZE_MIN (-__LONG_MAX__ -1L)

#define U8_MAX    0xffU
#define U16_MAX   0xffffU
#define U32_MAX   0xffffffffU
#define U64_MAX   0xffffffffffffffffULL
#ifdef __SIZE_MAX__
  #define USIZE_MAX __SIZE_MAX__
#else
  #define USIZE_MAX (__LONG_MAX__ *2UL+1UL)
#endif

#ifndef INTPTR_MIN
  #ifdef __INTPTR_MAX__
    #define INTPTR_MIN  (-__INTPTR_MAX__-1L)
    #define INTPTR_MAX  __INTPTR_MAX__
    #define UINTPTR_MAX __UINTPTR_MAX__
  #else
    #define INTPTR_MIN  ISIZE_MIN
    #define INTPTR_MAX  ISIZE_MAX
    #define UINTPTR_MAX USIZE_MAX
  #endif
#endif

// isize
#if USIZE_MAX == __LONG_MAX__
typedef long isize;
#elif USIZE_MAX >= 0xffffffffffffffff
typedef i64 isize;
#elif USIZE_MAX >= 0xffffffff
typedef i32 isize;
#elif USIZE_MAX >= 0xffff
typedef i16 isize;
#elif USIZE_MAX >= 0xff
typedef i8 isize;
#endif


// compiler feature test macros
#ifndef __has_attribute
  #define __has_attribute(x)  0
#endif
#ifndef __has_extension
  #define __has_extension   __has_feature
#endif
#ifndef __has_feature
  #define __has_feature(x)  0
#endif
#ifndef __has_include
  #define __has_include(x)  0
#endif
#ifndef __has_builtin
  #define __has_builtin(x)  0
#endif

// nullability
#ifndef NULL
  #define NULL ((void*)0)
#endif
#if __has_feature(nullability)
  #define nonull _Nonnull
#else
  #define nonull _Nonnull
#endif
#if defined(__clang__) && __has_feature(nullability)
  #ifndef nullable
    #define nullable _Nullable
  #endif
  #define ASSUME_NONNULL_BEGIN                                                \
    _Pragma("clang diagnostic push")                                              \
    _Pragma("clang diagnostic ignored \"-Wnullability-completeness\"")            \
    _Pragma("clang diagnostic ignored \"-Wnullability-inferred-on-nested-type\"") \
    _Pragma("clang assume_nonnull begin")
  #define ASSUME_NONNULL_END    \
    _Pragma("clang diagnostic pop") \
    _Pragma("clang assume_nonnull end")
#else
  #ifndef nullable
    #define nullable
  #endif
  #define ASSUME_NONNULL_BEGIN
  #define ASSUME_NONNULL_END
#endif

#ifdef __cplusplus
  #define NORETURN [[noreturn]]
#else
  #define NORETURN      _Noreturn
  #define auto          __auto_type
  #define static_assert _Static_assert
#endif

#if __has_attribute(fallthrough)
  #define FALLTHROUGH __attribute__((fallthrough))
#else
  #define FALLTHROUGH
#endif

#if __has_attribute(musttail) && !defined(__wasm__)
  // Note on "!defined(__wasm__)": clang 13 claims to have this attribute for wasm
  // targets but it's actually not implemented and causes an error.
  #define MUSTTAIL __attribute__((musttail))
#else
  #define MUSTTAIL
#endif

#ifndef thread_local
  #define thread_local _Thread_local
#endif

#ifdef __cplusplus
  #define EXTERN_C extern "C"
#else
  #define EXTERN_C
#endif

// ATTR_FORMAT(archetype, string-index, first-to-check)
// archetype determines how the format string is interpreted, and should be printf, scanf,
// strftime or strfmon.
// string-index specifies which argument is the format string argument (starting from 1),
// while first-to-check is the number of the first argument to check against the
// format string. For functions where the arguments are not available to be checked
// (such as vprintf), specify the third parameter as zero.
#if __has_attribute(format)
  #define ATTR_FORMAT(...) __attribute__((format(__VA_ARGS__)))
#else
  #define ATTR_FORMAT(...)
#endif

#if __has_attribute(always_inline)
  #define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
  #define ALWAYS_INLINE inline
#endif

#if __has_attribute(noinline)
  #define NO_INLINE __attribute__((noinline))
#else
  #define NO_INLINE
#endif

#if __has_attribute(unused)
  #define UNUSED __attribute__((unused))
#else
  #define UNUSED
#endif

#if __has_attribute(used)
  #define ATTR_USED __attribute__((used))
#else
  #define ATTR_USED
#endif

#if __has_attribute(warn_unused_result)
  #define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
  #define WARN_UNUSED_RESULT
#endif

#if __has_attribute(__packed__)
  #define ATTR_PACKED __attribute__((__packed__))
#else
  #define ATTR_PACKED
#endif

#if __has_attribute(malloc)
  #define ATTR_MALLOC __attribute__((malloc))
#else
  #define ATTR_MALLOC
#endif
#if __has_attribute(alloc_size)
  // void *my_malloc(int a) ATTR_ALLOC_SIZE(1);
  // void *my_calloc(int a, int b) ATTR_ALLOC_SIZE(1, 2);
  #define ATTR_ALLOC_SIZE(args...) __attribute__((alloc_size(args)))
#else
  #define ATTR_ALLOC_SIZE(...)
#endif

#if __has_feature(address_sanitizer)
  // https://clang.llvm.org/docs/AddressSanitizer.html
  #define ASAN_ENABLED 1
  #define ASAN_DISABLE_ADDR_ATTR __attribute__((no_sanitize("address")))
#else
  #define ASAN_DISABLE_ADDR_ATTR
#endif

#if __has_attribute(no_sanitize)
  #define ATTR_NOSAN(str) __attribute__((no_sanitize(str)))
#else
  #define ATTR_NOSAN(str) __attribute__((no_sanitize(str)))
#endif

// _Noreturn abort()
#ifndef CO_NO_LIBC
  #include <stdlib.h> // void abort(void)
#elif __has_builtin(__builtin_trap)
  #define abort __builtin_trap
#elif __has_builtin(__builtin_unreachable)
  #define abort __builtin_unreachable()
#else
  #error no abort()
#endif

#if __has_builtin(__builtin_unreachable)
  #define UNREACHABLE __builtin_unreachable()
#elif __has_builtin(__builtin_trap)
  #define UNREACHABLE __builtin_trap
#else
  #define UNREACHABLE abort()
#endif

// UNLIKELY(integralexpr)->bool
#if __has_builtin(__builtin_expect)
  #define LIKELY(x)   (__builtin_expect((bool)(x), true))
  #define UNLIKELY(x) (__builtin_expect((bool)(x), false))
#else
  #define LIKELY(x)   (x)
  #define UNLIKELY(x) (x)
#endif

#if defined(__clang__) || defined(__gcc__)
  #define _DIAGNOSTIC_IGNORE_PUSH(x)  _Pragma("GCC diagnostic push") _Pragma(#x)
  #define DIAGNOSTIC_IGNORE_PUSH(STR) _DIAGNOSTIC_IGNORE_PUSH(GCC diagnostic ignored STR)
  #define DIAGNOSTIC_IGNORE_POP()     _Pragma("GCC diagnostic pop")
#else
  #define DIAGNOSTIC_IGNORE_PUSH(STR)
  #define DIAGNOSTIC_IGNORE_POP()
#endif

#ifndef offsetof
  #if __has_builtin(__builtin_offsetof)
    #define offsetof __builtin_offsetof
  #else
    #define offsetof(st, m) ((usize)&(((st*)0)->m))
  #endif
#endif

#ifndef alignof
  #define alignof _Alignof
#endif

#ifndef alignas
  #define alignas _Alignas
#endif

#ifndef countof
  #define countof(x) \
    ((sizeof(x)/sizeof(0[x])) / ((usize)(!(sizeof(x) % sizeof(0[x])))))
#endif

#define CONCAT_(x,y) x##y
#define CONCAT(x,y)  CONCAT_(x,y)

#define MAX(a,b) \
  ({__typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })
  // turns into CMP + CMOV{L,G} on x86_64
  // turns into CMP + CSEL on arm64

#define MIN(a,b) \
  ({__typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a < _b ? _a : _b; })
  // turns into CMP + CMOV{L,G} on x86_64
  // turns into CMP + CSEL on arm64

// XMAX & XMIN -- for use only with constant expressions
#define XMAX(x,y) ((x)>(y)?(x):(y))
#define XMIN(x,y) ((x)<(y)?(x):(y))

// SET_FLAG(int flags, int flag, bool on)
// equivalent to: if (on) flags |= flag; else flags &= ~flag
#define SET_FLAG(flags, flag, on) (flags ^= (-(!!(on)) ^ (flags)) & (flag))

// T ALIGN2<T>(T x, anyuint a)       rounds up x to nearest a (a must be a power of two)
// T ALIGN2_FLOOR<T>(T x, anyuint a) rounds down x to nearest a
// bool IS_ALIGN2(T x, anyuint a)    true if x is aligned to a
#define ALIGN2(x,a)           _ALIGN2_MASK(x, (__typeof__(x))(a) - 1)
#define ALIGN2_FLOOR(x, a)    ALIGN2((x) - ((a) - 1), (a))
#define IS_ALIGN2(x, a)       (((x) & ((__typeof__(x))(a) - 1)) == 0)
#define _ALIGN2_MASK(x, mask) (((x) + (mask)) & ~(mask))

// co_ctz returns the number of trailing 0-bits in x,
// starting at the least significant bit position. If x is 0, the result is undefined.
#define co_ctz(x) _Generic((x), \
  u32:   __builtin_ctz,       \
  usize: __builtin_ctzl,      \
  u64:   __builtin_ctzll)(x)

// END_ENUM(NAME) should be placed at an enum that has a matching integer typedef.
// Example 1:
//   typedef u16 foo;
//   enum foo { fooA, fooB = 0xff, fooC = 0xfff } END_ENUM(foo);
//   // ok; no error since fooC fits in u16
// Example 2:
//   typedef u8 foo; // too small for fooC value
//   enum foo { fooA, fooB = 0xff, fooC = 0xfff } END_ENUM(foo);
//   // error: static_assert failed due to requirement
//   // 'sizeof(enum foo) <= sizeof(unsigned char)' "too many foo values"
//
#if __has_attribute(__packed__) && !defined(__cplusplus)
  #define END_ENUM(NAME) \
    __attribute__((__packed__));  \
    static_assert(sizeof(enum NAME) <= sizeof(NAME), "too many " #NAME " values");
#else
  #define END_ENUM(NAME) ;
#endif

#ifdef __cplusplus
  #define DEF_ENUM(NAME) enum NAME##E
#else
  #define DEF_ENUM(NAME) enum NAME
#endif

// u32 CAST_U32(anyint z) => [0-U32_MAX]
#define CAST_U32(z) ({ \
  __typeof__(z) z__ = (z); \
  sizeof(u32) < sizeof(z__) ? (u32)MIN((__typeof__(z__))U32_MAX,z__) : (u32)z__; \
})

// __fls(uint n) finds the last (most-significant) bit set
#define __fls(n) ((sizeof(n) <= 4) ? __fls32(n) : __fls64(n))
static ALWAYS_INLINE int __fls32(unsigned int x) {
  return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}
static ALWAYS_INLINE unsigned long __flsl(unsigned long x) {
  return (sizeof(x) * 8) - 1 - __builtin_clzl(x);
}
#if USIZE_MAX < 0xffffffffffffffff
  static ALWAYS_INLINE int __fls64(u64 x) {
    u32 h = x >> 32;
    if (h)
      return __fls32(h) + 32;
    return __fls32(x);
  }
#else
  static ALWAYS_INLINE int __fls64(u64 x) {
    if (x == 0)
      return 0;
    return __flsl(x) + 1;
  }
#endif

static inline WARN_UNUSED_RESULT bool __must_check_unlikely(bool unlikely) {
  return UNLIKELY(unlikely);
}


// —————————————————————————————————————————————————————————————————————————————————————
// BEGIN code adapted from Linux.
// The code listed on the following lines up until "END code adapted from Linux"
// is licensed under the MIT license: (<linux-src>/LICENSES/preferred/MIT)
//
// MIT License
//
// Copyright (c) <year> <copyright holders>
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#define __same_type(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))

// #define _ALIGN1(x, a)          _ALIGN1_MASK(x, (__typeof__(x))(a) - 1)
// #define _ALIGN1_MASK(x, mask)  (((x) + (mask)) & ~(mask))
// #define ALIGN(x, a)            _ALIGN1((x), (a))
// #define ALIGN_DOWN(x, a)       _ALIGN1((x) - ((a) - 1), (a))
// #define PTR_ALIGN(p, a)        ((__typeof__(p))ALIGN((unsigned long)(p), (a)))
// #define PTR_ALIGN_DOWN(p, a)   ((__typeof__(p))ALIGN_DOWN((unsigned long)(p), (a)))
// #define IS_ALIGNED(x, a)       (((x) & ((__typeof__(x))(a) - 1)) == 0)

// a + b => d
#define check_add_overflow(a, b, d) __must_check_unlikely(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_add_overflow(__a, __b, __d);  \
}))

// a - b => d
#define check_sub_overflow(a, b, d) __must_check_unlikely(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_sub_overflow(__a, __b, __d);  \
}))

// a * b => d
#define check_mul_overflow(a, b, d) __must_check_unlikely(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_mul_overflow(__a, __b, __d);  \
}))

// Compute a*b+c, returning USIZE_MAX on overflow. Internal helper for STRUCT_SIZE() below.
static inline WARN_UNUSED_RESULT usize __ab_c_size(usize a, usize b, usize c) {
  usize bytes;
  if (check_mul_overflow(a, b, &bytes))
    return USIZE_MAX;
  if (check_add_overflow(bytes, c, &bytes))
    return USIZE_MAX;
  return bytes;
}

// STRUCT_SIZE calculates size of structure with trailing array, checking for overflow.
// p      Pointer to the structure
// member Name of the array member
// count  Number of elements in the array
//
// Calculates size of memory needed for structure p followed by an array of count number
// of member elements.
// Returns number of bytes needed or USIZE_MAX on overflow.
#define STRUCT_SIZE(p, member, count)                    \
  __ab_c_size(count,                                     \
    sizeof(*(p)->member) + __must_be_array((p)->member), \
    sizeof(*(p)))

// array_size calculates size of 2-dimensional array (i.e. a * b)
// Returns number of bytes needed to represent the array or USIZE_MAX on overflow.
static inline WARN_UNUSED_RESULT usize array_size(usize a, usize b) {
  usize bytes;
  if (check_mul_overflow(a, b, &bytes))
    return USIZE_MAX;
  return bytes;
}

// BUILD_BUG_ON_ZERO is a neat trick used in the Linux kernel source to force a
// compilation error if condition is true, but also produce a result
// (of value 0 and type int), so the expression can be used e.g. in a structure
// initializer (or where-ever else comma expressions aren't permitted).
#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int:(-!!(e)); })))

#define __must_be_array(a) BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))

// ARRAY_LEN: number of elements of an array
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

// ILOG2 calculates the log of base 2
#define ILOG2(n) ( \
  __builtin_constant_p(n) ? ((n) < 2 ? 0 : 63 - __builtin_clzll(n)) : __fls(n) - 1 )

// CEIL_POW2 rounds up n to nearest power of two. Result is undefined when n is 0.
#define CEIL_POW2(n) ( \
  __builtin_constant_p(n) ? ( ((n) == 1) ? 1 : (1UL << (ILOG2((n) - 1) + 1)) ) \
                          : (1UL << __fls(n - 1)) )

// END code adapted from Linux
// —————————————————————————————————————————————————————————————————————————————————————

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

// ASSERT_U32SIZE checks that size is <= U32_MAX at compile time if possible,
// or with a runtime assertion if size is not a constant expression.
//#define ASSERT_U32SIZE(size) \
//  ( __builtin_constant_p(size) ? \
//    BUILD_BUG_ON_ZERO( XMIN((u64)(size), (u64)U32_MAX+1) == (u64)U32_MAX+1 ) : \
//    assert((u64)(size) <= (u64)U32_MAX) )
#define ASSERT_U32SIZE(size) \
  assert((u64)(size) <= (u64)U32_MAX) // TODO: above constexpr

#define BEGIN_INTERFACE \
  ASSUME_NONNULL_BEGIN \
  DIAGNOSTIC_IGNORE_PUSH("-Wunused-function")

#define END_INTERFACE \
  DIAGNOSTIC_IGNORE_POP() \
  ASSUME_NONNULL_END

// assume pointer types are "nonull"
ASSUME_NONNULL_BEGIN

// CO_LITTLE_ENDIAN=0|1
#ifndef CO_LITTLE_ENDIAN
  #if defined(__LITTLE_ENDIAN__) || \
      (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    #define CO_LITTLE_ENDIAN 1
  #elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) \
        (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    #define CO_LITTLE_ENDIAN 0
  #else
    #error Can't determine endianness -- please define CO_LITTLE_ENDIAN=0|1
  #endif
#endif

#if __has_builtin(__builtin_bswap32)
  #define bswap32(x) __builtin_bswap32(x)
#elif defined(_MSC_VER)
  #define bswap32(x) _byteswap_ulong(x)
#else
  static inline u32 bswap32(u32 x) {
    return ((( x & 0xff000000u ) >> 24 )
          | (( x & 0x00ff0000u ) >> 8  )
          | (( x & 0x0000ff00u ) << 8  )
          | (( x & 0x000000ffu ) << 24 ));
  }
#endif

#if __has_builtin(__builtin_bswap64)
  #define bswap64(x) __builtin_bswap64(x)
#elif defined(_MSC_VER)
  #define bswap64(x) _byteswap_uint64(x)
#else
  static inline u64 bswap64(u64 x) {
    u64 hi = bswap32((u32)x);
    u32 lo = bswap32((u32)(x >> 32));
    return (hi << 32) | lo;
  }
#endif

#if CO_LITTLE_ENDIAN
  #define htole32(n) (n)
  #define htobe32(n) bswap32(n)
  #define htole64(n) (n)
  #define htobe64(n) bswap64(n)
#else
  #define htole32(n) bswap32(n)
  #define htobe32(n) (n)
  #define htole64(n) bswap64(n)
  #define htobe64(n) (n)
#endif

// —————————————————————————————————————————————————————————————————————————————————————
// libc

#define fabs   __builtin_fabs
#define sinf   __builtin_sinf
#define cosf   __builtin_cosf
#define floor  __builtin_floor
#define ceil   __builtin_ceil

#define memset  __builtin_memset
#define memcpy  __builtin_memcpy
#define memcmp  __builtin_memcmp
#define memchr  __builtin_memchr
#define memmove __builtin_memmove

#define strlen __builtin_strlen
#define strcmp __builtin_strcmp

typedef __builtin_va_list va_list;
#ifndef va_start
  #define va_start __builtin_va_start
  #define va_end   __builtin_va_end
  #define va_arg   __builtin_va_arg
  #define va_copy  __builtin_va_copy
#endif

char* strstr(const char* haystack, const char* needle);

// —————————————————————————————————————————————————————————————————————————————————————
ASSUME_NONNULL_END

#include "error.h"
#include "debug.h"

#ifndef __cplusplus

#include "test.h"

#include "qsort.h"
#include "mem.h"
#include "time.h"

#include "array.h"
#include "string.h"
#include "hash.h"
#include "map.h"

#include "sys.h"
#include "path.h"

#include "sym.h"
#include "tstyle.h"
#include "sha256.h"
#include "unicode.h"

#endif // !defined(__cplusplus)
// —————————————————————————————————————————————————————————————————————————————————————
#endif // CO_LIB
