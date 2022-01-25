// This files contains definitions used across the entire codebase. Keep it lean.
#pragma once

#ifndef __cplusplus
  typedef _Bool bool;
  #define true  ((bool)1)
  #define false ((bool)0)
#endif
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
typedef signed long            isize;
typedef unsigned long          usize;
typedef signed long            intptr;
typedef unsigned long          uintptr;

#ifndef NULL
  #define NULL ((void*)0)
#endif

#define I8_MAX    0x7f
#define I16_MAX   0x7fff
#define I32_MAX   0x7fffffff
#define I64_MAX   0x7fffffffffffffff
#define ISIZE_MAX __LONG_MAX__

#define I8_MIN    (-0x80)
#define I16_MIN   (-0x8000)
#define I32_MIN   (-0x80000000)
#define I64_MIN   (-0x8000000000000000)
#define ISIZE_MIN (-__LONG_MAX__ -1L)

#define U8_MAX    0xff
#define U16_MAX   0xffff
#define U32_MAX   0xffffffff
#define U64_MAX   0xffffffffffffffff
#define USIZE_MAX (__LONG_MAX__ *2UL+1UL)

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
#if defined(__clang__) && __has_feature(nullability)
  #define __NULLABILITY_PRAGMA_PUSH \
    _Pragma("clang diagnostic push")  \
    _Pragma("clang diagnostic ignored \"-Wnullability-completeness\"") \
    _Pragma("clang diagnostic ignored \"-Wnullability-inferred-on-nested-type\"")
  #define __NULLABILITY_PRAGMA_POP _Pragma("clang diagnostic pop")
  #define ASSUME_NONNULL_BEGIN __NULLABILITY_PRAGMA_PUSH _Pragma("clang assume_nonnull begin")
  #define ASSUME_NONNULL_END   __NULLABILITY_PRAGMA_POP  _Pragma("clang assume_nonnull end")
#else
  #define _Nullable
  #define _Nonnull
  #define _Null_unspecified
  #define __NULLABILITY_PRAGMA_PUSH
  #define __NULLABILITY_PRAGMA_POP
  #define ASSUME_NONNULL_BEGIN
  #define ASSUME_NONNULL_END
#endif
#define nullable      _Nullable
#define nonull        _Nonnull
#define nonnullreturn __attribute__((returns_nonnull))

#ifdef __cplusplus
  #define NORETURN noreturn
#else
  #define NORETURN      _Noreturn
  #define static_assert _Static_assert
#endif

#if __has_attribute(fallthrough)
  #define FALLTHROUGH __attribute__((fallthrough))
#else
  #define FALLTHROUGH
#endif

#if __has_attribute(musttail)
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
// while first-to-check is the number of the first argument to check against the format string.
// For functions where the arguments are not available to be checked (such as vprintf),
// specify the third parameter as zero.
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
  #define USED __attribute__((used))
#else
  #define USED
#endif

#if __has_attribute(warn_unused_result)
  #define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
  #define WARN_UNUSED_RESULT
#endif

#if __has_attribute(malloc)
  #define ATTR_MALLOC __attribute__((malloc))
#else
  #define ATTR_MALLOC
#endif
#if __has_attribute(alloc_size)
  #define ATTR_ALLOC_SIZE(whicharg) __attribute__((alloc_size(whicharg)))
#else
  #define ATTR_ALLOC_SIZE(whicharg)
#endif

#if __has_feature(address_sanitizer)
  // https://clang.llvm.org/docs/AddressSanitizer.html
  #define ASAN_ENABLED 1
  #define ASAN_DISABLE_ADDR_ATTR __attribute__((no_sanitize("address"))) /* function attr */
#else
  #define ASAN_DISABLE_ADDR_ATTR
#endif

#if __has_builtin(__builtin_unreachable)
  #define UNREACHABLE __builtin_unreachable()
#elif defined(CO_WITH_LIBC)
  void abort(void); // stdlib.h
  #define UNREACHABLE abort()
#else
  #error no UNREACHABLE support
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

// UNLIKELY(integralexpr)->integralexpr
#ifdef __builtin_expect
  #define UNLIKELY(x) __builtin_expect((x), 0)
  #define LIKELY(x)   __builtin_expect((x), 1)
#else
  #define UNLIKELY(x) (x)
  #define LIKELY(x)   (x)
#endif

// T ALIGN2<T>(T x, anyuint a)       rounds up x to nearest a (a must be a power of two)
// T ALIGN2_FLOOR<T>(T x, anyuint a) rounds down x to nearest a
// bool IS_ALIGN2(T x, anyuint a)    true if x is aligned to a
#define ALIGN2(x,a)           _ALIGN2_MASK(x, (__typeof__(x))(a) - 1)
#define ALIGN2_DOWN(x, a)     ALIGN2((x) - ((a) - 1), (a))
#define IS_ALIGN2(x, a)       (((x) & ((__typeof__(x))(a) - 1)) == 0)
#define _ALIGN2_MASK(x, mask) (((x) + (mask)) & ~(mask))

// END_TYPED_ENUM(NAME) should be placed at an enum that has a matching integer typedef.
// Example 1:
//   typedef u16 foo;
//   enum foo { fooA, fooB = 0xff, fooC = 0xfff } END_TYPED_ENUM(foo);
//   // ok; no error since fooC fits in u16
// Example 2:
//   typedef u8 foo; // too small for fooC value
//   enum foo { fooA, fooB = 0xff, fooC = 0xfff } END_TYPED_ENUM(foo);
//   // error: static_assert failed due to requirement
//   // 'sizeof(enum foo) <= sizeof(unsigned char)' "too many foo values"
//
#if __has_attribute(__packed__)
  #define END_TYPED_ENUM(NAME)   \
    __attribute__((__packed__)); \
    static_assert(sizeof(enum NAME) <= sizeof(NAME), "too many " #NAME " values");
#else
  #define END_TYPED_ENUM(NAME) ;
#endif


// ======================================================================================
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

static inline WARN_UNUSED_RESULT bool __must_check_overflow(bool overflow) {
  return UNLIKELY(overflow);
}

// a + b => d
#define check_add_overflow(a, b, d) __must_check_overflow(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_add_overflow(__a, __b, __d);  \
}))

// a - b => d
#define check_sub_overflow(a, b, d) __must_check_overflow(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_sub_overflow(__a, __b, __d);  \
}))

// a * b => d
#define check_mul_overflow(a, b, d) __must_check_overflow(({  \
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

// __fls finds the last (most-significant) bit set
#define __fls(x) (x ? sizeof(x) * 8 - __builtin_clz(x) : 0)

// ILOG2 calculates the log of base 2
#define ILOG2(n) (             \
  __builtin_constant_p(n) ?    \
    ((n) < 2 ? 0 :             \
    63 - __builtin_clzll(n)) : \
  __fls(n)                     \
)

// CEIL_POW2 rounds up n to nearest power of two. Result is undefined when n is 0.
#define CEIL_POW2(n) (              \
  __builtin_constant_p(n) ? (       \
    ((n) == 1) ? 1 :                \
      (1UL << (ILOG2((n) - 1) + 1)) \
    ) :                             \
    (1UL << __fls(n - 1))           \
)

// END code adapted from Linux
// ======================================================================================

// void assert(expr condition)
#if defined(DEBUG) || !defined(NDEBUG)
  #undef NDEBUG
  #ifdef CO_WITH_LIBC
    #include <assert.h>
  #else
    #define assert(cond) if (!(cond)) UNREACHABLE
  #endif
#else
  #define assert(...) ((void)0)
#endif

// void dlog(const char* fmt, ...)
#if defined(DEBUG)
  #ifndef CO_WITH_LIBC
    #warning dlog not implemented for no-libc
    #define dlog(format, ...) ((void)0)
  #else
    #include <stdio.h>
    #define dlog(format, ...) ({ \
      fprintf(stderr, "\e[1;34m[D]\e[0m " format " \e[2m(%s %d)\e[0m\n", \
        ##__VA_ARGS__, __FUNCTION__, __LINE__); \
      fflush(stderr); \
    })
  #endif
#else
  #define dlog(format, ...) ((void)0)
#endif

#define errlog(format, ...) (({ \
  fprintf(stderr, "error: " format " (%s:%d)\n", \
    ##__VA_ARGS__, __FILE__, __LINE__); \
  fflush(stderr); \
}))


ASSUME_NONNULL_BEGIN

// ======================================================================================
// error

typedef i32 error;
enum error {
  err_ok            =   0, // no error
  err_invalid       =  -1, // invalid data or argument
  err_sys_op        =  -2, // invalid syscall op or syscall op data
  err_badfd         =  -3, // invalid file descriptor
  err_bad_name      =  -4, // invalid or misformed name
  err_not_found     =  -5, // resource not found
  err_name_too_long =  -6, // name too long
  err_canceled      =  -7, // operation canceled
  err_not_supported =  -8, // not supported
  err_exists        =  -9, // already exists
  err_end           = -10, // end of resource
  err_access        = -11, // permission denied
  err_nomem         = -12, // cannot allocate memory
  err_mfault        = -13, // bad memory address
  err_overflow      = -14, // value too large for defined data type
};

error error_from_errno(int errno);
const char* error_str(error);


// ======================================================================================
// libc host-independent functions (just the parts we need)

#define fabs   __builtin_fabs
#define sinf   __builtin_sinf
#define cosf   __builtin_cosf
#define floor  __builtin_floor
#define ceil   __builtin_ceil

#define memset __builtin_memset
#define memcpy __builtin_memcpy
#define memcmp __builtin_memcmp
#define memchr __builtin_memchr
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


// ======================================================================================
// panic

// panic prints msg to stderr and calls abort()
#define panic(fmt, ...) _panic(__FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

NORETURN void _panic(const char* file, int line, const char* fun, const char* fmt, ...)
  ATTR_FORMAT(printf, 4, 5);


// ======================================================================================
// Mem -- heap memory allocator

typedef struct MemAllocator MemAllocator;
typedef const MemAllocator* nonull Mem;

// memalloc allocates memory of size bytes
static void* nullable memalloc(Mem m, usize size)
ATTR_MALLOC ATTR_ALLOC_SIZE(2) WARN_UNUSED_RESULT;

// T* nullable memalloct(Mem mem, type T)
// memalloct allocates memory the size of TYPE, returning a pointer of TYPE*
#define memalloct(mem, TYPE) ((TYPE*)memalloc((mem),sizeof(TYPE)))

// void* nullable memallocv(Mem mem, uint ELEMSIZE, uint COUNT)
// memallocv behaves similar to libc calloc, checking ELEMSIZE*COUNT for overflow.
// Returns NULL on overflow or allocation failure.
#define memallocv(mem, ELEMSIZE, COUNT) ({    \
  usize z = array_size((ELEMSIZE), (COUNT));  \
  z == USIZE_MAX ? NULL : memalloc((mem), z); \
})

// T* nullable memalloctv(Mem mem, type T, name VFIELD_NAME, uint VCOUNT)
// memalloctv allocates memory for struct type TYPE with variable number of tail elements.
// Returns NULL on overflow or allocation failure.
// e.g.
//   struct foo { int c; u8 v[]; }
//   struct foo* p = memalloctv(mem, struct foo, v, 3);
#define memalloctv(mem, TYPE, VFIELD_NAME, VCOUNT) ({         \
  usize z = STRUCT_SIZE( ((TYPE*)0), VFIELD_NAME, (VCOUNT) ); \
  z == USIZE_MAX ? NULL : (TYPE*)memalloc((mem), z);          \
})

// memrealloc resizes memory at ptr. If ptr is null, the behavior matches memalloc.
static void* nullable memrealloc(Mem m, void* nullable ptr, usize newsize)
ATTR_ALLOC_SIZE(3) WARN_UNUSED_RESULT;

// memfree frees memory allocated with memalloc
static void memfree(Mem m, void* nonull ptr);

// memdup2 makes a copy of src with optional extraspace at the end.
void* nullable mem_dup2(Mem m, const void* src, usize srclen, usize extraspace);

// memdup makes a copy of src
inline static void* nullable mem_dup(Mem m, const void* src, usize len) {
  return mem_dup2(m, src, len, 0);
}

// memstrdup is like strdup but uses m
char* nullable mem_strdup(Mem m, const char* cstr);

// MemAllocator is the implementation interface for an allocator.
typedef struct MemAllocator {
  // alloc should allocate at least size contiguous memory and return the address.
  // If it's unable to do so it should return NULL.
  void* nullable (* nonull alloc)(Mem m, usize size);

  // realloc is called with the address of a previous allocation of the same allocator m.
  // It should either extend the contiguous memory segment at ptr to be at least newsize
  // long in total, or allocate a new contiguous memory of at least newsize.
  // If it's unable to fulfill the request it should return NULL.
  // Note that ptr is never NULL; calls to memrealloc with a NULL ptr are routed to alloc.
  void* nullable (* nonull realloc)(Mem m, void* nullable ptr, usize newsize);

  // free is called with the address of a previous allocation of the same allocator m.
  // The allocator now owns the memory at ptr and may recycle it or release it to the system.
  void (* nonull free)(Mem m, void* nonull ptr);
} MemAllocator;


// user-buffer allocator
typedef struct MemBufAllocator {
  MemAllocator a;
  u8*   buf;
  usize cap;
  usize len;
} MemBufAllocator;

Mem mem_buf_allocator_init(MemBufAllocator* a, void* buf, usize size);

#define DEF_MEM_STACK_BUF_ALLOCATOR(name, buf)                       \
  MemBufAllocator _memstk_a = {0};                                   \
  Mem name = mem_buf_allocator_init(&_memstk_a, (buf), sizeof(buf));


// invalid allocator (panic on allocation)
static Mem mem_nil_allocator();
static void* nullable _mem_nil_alloc(Mem _, usize size) {
  panic("attempt to allocate memory with nil allocator");
  return NULL;
}
static void* nullable _mem_nil_realloc(Mem m, void* nullable ptr, usize newsize) {
  return _mem_nil_alloc(m, 0);
}
static void _mem_nil_free(Mem _, void* nonull ptr) {}
static const MemAllocator _mem_nil = {
  .alloc   = _mem_nil_alloc,
  .realloc = _mem_nil_realloc,
  .free    = _mem_nil_free,
};
inline static Mem mem_nil_allocator() {
  return &_mem_nil;
}


// libc allocator
#ifdef CO_WITH_LIBC
ASSUME_NONNULL_END
#include <stdlib.h>
ASSUME_NONNULL_BEGIN

// mem_libc_allocator returns a shared libc allocator (malloc, realloc & free from libc)
static Mem mem_libc_allocator();

/*
Note on mem_libc_allocator runtime overhead:
  Recent versions of GCC or Clang will optimize calls to memalloc(mem_libc_allocator(), z) into
  direct calls to the clib functions and completely eliminate the code declared in this file.
Example code generation:
  int main() {
    void* p = memalloc(mem_libc_allocator(), 12);
    return (int)p;
  }
  —————————————————————————————————————————————————————————
  x86_64 clang-12 -O2:  | arm64 clang-11 -O2:
    main:               |   main:
      mov     edi, 1    |     stp     x29, x30, [sp, #-16]!
      mov     esi, 12   |     mov     x29, sp
      jmp     calloc    |     mov     w0, #1
                        |     mov     w1, #12
                        |     bl      calloc
                        |     ldp     x29, x30, [sp], #16
                        |     ret
  —————————————————————————————————————————————————————————
  x86_64 gcc-11 -O2:    | arm64 gcc-11 -O2:
    main:               |   main:
      sub     rsp, 8    |     stp     x29, x30, [sp, -16]!
      mov     esi, 12   |     mov     x1, 12
      mov     edi, 1    |     mov     x0, 1
      call    calloc    |     mov     x29, sp
      add     rsp, 8    |     bl      calloc
      ret               |     ldp     x29, x30, [sp], 16
                        |     ret
  —————————————————————————————————————————————————————————
  https://godbolt.org/z/MK757acnK
*/
static void* nullable _mem_libc_alloc(Mem _, usize size) {
  return malloc(size);
}
static void* nullable _mem_libc_realloc(Mem _, void* nullable ptr, usize newsize) {
  return realloc(ptr, newsize);
}
static void _mem_libc_free(Mem _, void* nonull ptr) {
  free(ptr);
}
static const MemAllocator _mem_libc = {
  .alloc   = _mem_libc_alloc,
  .realloc = _mem_libc_realloc,
  .free    = _mem_libc_free,
};
inline static Mem mem_libc_allocator() {
  return &_mem_libc;
}

#endif // defined(CO_WITH_LIBC)

inline static void* nullable memalloc(Mem m, usize size) {
  assert(m != NULL);
  void* p = m->alloc(m, size);
  #ifdef CO_MEM_DEBUG_ALLOCATIONS
  dlog("[co memalloc] %p-%p (%zu)", p, p + size, size);
  #endif
  return p;
}

inline static void* nullable memrealloc(Mem m, void* nullable ptr, usize newsize) {
  assert(m != NULL);
  void* p = ptr ? m->realloc(m, ptr, newsize) : m->alloc(m, newsize);
  #ifdef CO_MEM_DEBUG_ALLOCATIONS
  dlog("[co realloc] %p -> %p-%p (%zu)", ptr, p, p + newsize, newsize);
  #endif
  return p;
}

// memfree frees memory allocated with memalloc
inline static void memfree(Mem m, void* nonull ptr) {
  assert(m != NULL);
  assert(ptr != NULL);
  #ifdef CO_MEM_DEBUG_ALLOCATIONS
  dlog("[co memfree] %p", ptr);
  #endif
  m->free(m, ptr);
}


// ======================================================================================
// Array -- dynamic linear container. Valid when zero-initialized.

typedef struct Array {
  void** v;           // entries
  u32    len;         // valid entries at v
  u32    cap : 31;    // capacity of v
  u32    onstack : 1; // true if v is space on stack (TODO: move into a single bit of len)
} Array;

#define ARRAY_INIT { NULL, 0, 0, false }
#define ARRAY_INIT_WITH_STORAGE(storage, initcap) { (storage), (initcap), 0, true }

static void  array_init(Array* a);
static void  array_init_storage(Array* a, void* storage, u32 storagecap);
static void  array_free(Array* a, Mem mem);
static void  array_clear(Array* a); // sets len to 0
error        array_grow(Array* a, u32 addl, Mem mem); // cap=align2(len+addl)
static bool  array_push(Array* a, void* nullable v, Mem mem); // false on overflow
static void* array_pop(Array* a);
void         array_remove(Array* a, u32 start, u32 count);
isize        array_indexof(Array* a, void* nullable entry); // -1 on failure
isize        array_lastindexof(Array* a, void* nullable entry); // -1 on failure

// array_copy copies src of srclen to a, starting at a.v[start], growing a if needed using m.
void array_copy(Array* a, u32 start, const void* src, u32 srclen, Mem m);

// The comparison function must return an integer less than, equal to, or greater than zero if
// the first argument is considered to be respectively less than, equal to, or greater than the
// second.
typedef int (*array_sort_fun)(const void* elem1, const void* elem2, void* nullable userdata);

// ArraySort sorts the array in place using comparator to rank entries
void array_sort(Array* a, array_sort_fun comparator, void* nullable userdata);

// ARRAY_FOR_EACH(const Array* a, ELEMTYPE, LOCALNAME) { use LOCALNAME }
//
// Note: It may be easier to just use a for loop:
//   for (u32 i = 0; i < a->len; i++) { auto ent = a->v[i]; ... }
//
#define ARRAY_FOR_EACH(a, ELEMTYPE, LOCALNAME)                         \
  /* avoid LOCALNAME clashing with a */                                \
  for (const Array* a__ = (a), * a1__ = (a); a__ == a1__; a1__ = NULL) \
    /* this for introduces LOCALNAME */                                \
    for (auto LOCALNAME = (ELEMTYPE)a__->v[0];                         \
         LOCALNAME == (ELEMTYPE)a__->v[0];                             \
         LOCALNAME++)                                                  \
      /* actual for loop */                                            \
      for (                                                            \
        u32 LOCALNAME##__i = 0,                                        \
            LOCALNAME##__end = a__->len;                               \
        LOCALNAME = (ELEMTYPE)a__->v[LOCALNAME##__i],                  \
        LOCALNAME##__i < LOCALNAME##__end;                             \
        LOCALNAME##__i++                                               \
      ) /* <body should follow here> */

// DEF_TYPED_ARRAY_FUNCTIONS(typename ST, name STFIELD, typename ELEMT, name FNPREFIX)
// Defines inline functions for a typed Array.
//
// Example:
//   struct IntArray {
//     Array a;
//   };
//   DEF_TYPED_ARRAY_FUNCTIONS(IntArray, a, int, intarray)
//   void example() {
//     IntArray a = {0};
//     intarray_init(&a);
//     intarray_push(&a, 10, mem);
//     intarray_push(&a, 20, mem);
//     printf("%d", intarray_at(&a, 1)); // "20"
//   }
//
// static void   FNPREFIX_init(ST* a);
// static void   FNPREFIX_init_storage(ST* a, ELEMT* storage, u32 storagecap);
// static void   FNPREFIX_free(ST* a, Mem mem);
// static void   FNPREFIX_clear(ST* a);
// static error  FNPREFIX_grow(ST* a, u32 addl, Mem mem);
// static bool   FNPREFIX_push(ST* a, ELEMT* nullable v, Mem mem);
// static ELEMT* FNPREFIX_pop(ST* a);
// static void   FNPREFIX_remove(ST* a, u32 start, u32 count);
// static isize  FNPREFIX_indexof(ST* a, ELEMT* nullable entry);
// static isize  FNPREFIX_lastindexof(ST* a, ELEMT* nullable entry);
// static ELEMT* FNPREFIX_at(ST* a, u32 index);
//
#define DEF_TYPED_ARRAY_FUNCTIONS(ST, STFIELD, ELEMT, FNPREFIX)                     \
inline static void FNPREFIX##_init(ST* a) {                                         \
  array_init(&a->STFIELD); }                                                        \
inline static void FNPREFIX##_init_storage(ST* a, ELEMT* storage, u32 storagecap) { \
  array_init_storage(&a->STFIELD, storage, storagecap); }                           \
inline static void FNPREFIX##_free(ST* a, Mem mem) {                                \
  array_free(&a->STFIELD, mem); }                                                   \
inline static void FNPREFIX##_clear(ST* a) {                                        \
  array_clear(&a->STFIELD); }                                                       \
inline static error FNPREFIX##_grow(ST* a, u32 addl, Mem mem) {                     \
  return array_grow(&a->STFIELD, addl, mem); }                                      \
inline static bool FNPREFIX##_push(ST* a, ELEMT* nullable v, Mem mem) {             \
  return array_push(&a->STFIELD, v, mem); }                                         \
inline static ELEMT* FNPREFIX##_pop(ST* a) {                                        \
  return array_pop(&a->STFIELD); }                                                  \
inline static void FNPREFIX##_remove(ST* a, u32 start, u32 count) {                 \
  array_remove(&a->STFIELD, start, count); }                                        \
inline static isize FNPREFIX##_indexof(ST* a, ELEMT* nullable entry) {              \
  return array_indexof(&a->STFIELD, entry); }                                       \
inline static isize FNPREFIX##_lastindexof(ST* a, ELEMT* nullable entry) {          \
  return array_lastindexof(&a->STFIELD, entry); }                                   \
inline static ELEMT* FNPREFIX##_at(ST* a, u32 index) {                              \
  return a->STFIELD.v[index]; }                                                     \
// end DEF_TYPED_ARRAY_FUNCTIONS

// U32Array
typedef struct { Array a; } U32Array;
DEF_TYPED_ARRAY_FUNCTIONS(U32Array, a, u32, u32array);

// ---- inline implementations (rest in array.c)

inline static void array_init(Array* a) {
  a->v = NULL;
  a->cap = 0;
  a->len = 0;
  a->onstack = false;
}

inline static void array_init_storage(Array* a, void* ptr, u32 cap) {
  a->v = (void**)ptr;
  a->cap = cap;
  a->len = 0;
  a->onstack = true;
}

inline static void array_free(Array* a, Mem mem) {
  if (!a->onstack && a->v != NULL)
    memfree(mem, a->v);
  #if DEBUG
  memset(a, 0, sizeof(*a));
  #endif
}

ALWAYS_INLINE static void array_clear(Array* a) {
  a->len = 0;
}

ALWAYS_INLINE static bool array_push(Array* a, void* nullable v, Mem mem) {
  if (UNLIKELY(a->len == a->cap) && array_grow(a, 1, mem) != 0)
    return false;
  a->v[a->len++] = v;
  return true;
}

ALWAYS_INLINE static void* array_pop(Array* a) {
  assert(a->len > 0);
  return a->v[--a->len];
}



// ======================================================================================
// Unicode

typedef i32 Rune;

static const Rune RuneErr  = 0xFFFD; // Unicode replacement character
static const Rune RuneSelf = 0x80;
  // characters below RuneSelf are represented as themselves in a single byte.
static const u32 UTF8Max = 4; // Maximum number of bytes of a UTF8-encoded char.

Rune utf8_decode(const u8* src, usize srclen, u32* width_out);



// ======================================================================================
// Str

typedef struct Str* Str;

struct Str {
  Mem  mem;
  u32  len;
  u32  cap;
  char p[];
} __attribute__ ((__packed__));

// str functions which return nullable Str returns NULL on memalloc failure or overflow

Str nullable str_make(Mem, u32 cap);
Str nullable str_make_copy(Mem, const char* src, u32 srclen);
static Str nullable str_make_cstr(Mem, const char* src_cstr);
Str nullable str_make_fmt(Mem, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);
Str nullable str_make_hex(Mem, const u8* data, u32 len);
Str nullable str_make_hex_lc(Mem, const u8* data, u32 len);
void str_free(Str);

inline static u32 str_avail(const Str s) { return s->cap - s->len; }
static Str nullable str_makeroom(Str s, u32 addlen); // ensures that str_avail()>=addlen
Str nullable str_grow(Str s, u32 addlen); // grow s->cap by at least addlen

Str nullable str_appendn(Str dst, const char* src, u32 srclen);
Str nullable str_appendc(Str dst, char c);
static Str nullable str_appendstr(Str dst, const Str src);
static Str nullable str_appendcstr(Str dst, const char* cstr);
Str nullable str_appendfmt(Str dst, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);
Str nullable str_appendfmtv(Str dst, const char* fmt, va_list);
Str nullable str_appendfill(Str dst, u32 n, char c);
Str nullable str_appendhex(Str dst, const u8* data, u32 len);
Str nullable str_appendhex_lc(Str dst, const u8* data, u32 len); // lower-case
Str nullable str_appendu64(Str dst, u64 v, u32 base);

// str_appendrepr appends a human-readable representation of data to dst as C-format ASCII
// string literals, with "special" bytes escaped (e.g. \n, \xFE, etc.)
// See str_appendhex as an alternative for non-text data.
Str str_appendrepr(Str s, const char* data, u32 len);

// Str str_append<T>(Str dst, T src) where T is a char or char*
#define str_append(dst_str, src) _Generic((src), \
  char:        str_appendc, \
  int:         str_appendc, \
  const char*: str_appendcstr, \
  char*:       str_appendcstr \
)(dst_str, src)

inline static Str str_setlen(Str s, u32 len) {
  assert(len <= s->cap);
  s->len = len;
  s->p[len] = 0;
  return s;
}
inline static Str str_trunc(Str s) { return str_setlen(s, 0); }


// --- inline implementation ---

inline static Str str_make_cstr(Mem mem, const char* src_cstr) {
  return str_make_copy(mem, src_cstr, strlen(src_cstr));
}
inline static Str str_makeroom(Str s, u32 addlen) {
  if (s->cap - s->len < addlen)
    return str_grow(s, addlen);
  return s;
}
inline static Str str_appendstr(Str s, const Str suffix) {
  return str_appendn(s, suffix->p, suffix->len);
}
inline static Str str_appendcstr(Str s, const char* cstr) {
  return str_appendn(s, cstr, strlen(cstr));
}



// ======================================================================================
// os

// os_getcwd populates buf with the current working directory including a nul terminator.
// If bufcap is not enough, an error is returned.
error os_getcwd(char* nonull buf, usize bufcap);



// ======================================================================================
// path

#ifdef WIN32
  #define PATH_SEPARATOR     '\\'
  #define PATH_SEPARATOR_STR "\\"
  #define PATH_DELIMITER     ';'
  #define PATH_DELIMITER_STR ";"
#else
  #define PATH_SEPARATOR     '/'
  #define PATH_SEPARATOR_STR "/"
  #define PATH_DELIMITER     ':'
  #define PATH_DELIMITER_STR ":"
#endif

// path_cwdrel returns path relative to the current working directory,
// or path verbatim if path is outside the working directory.
const char* path_cwdrel(const char* path);

// path_isabs returns true if path is an absolute path
bool path_isabs(const char* path);



// ======================================================================================
// fs

// fs_dirent.type values
enum fs_dirent_type {
  FS_DIRENT_UNKNOWN = 0,  // unknown
  FS_DIRENT_FIFO    = 1,  // named pipe or FIFO
  FS_DIRENT_CHR     = 2,  // character device
  FS_DIRENT_DIR     = 4,  // directory
  FS_DIRENT_BLK     = 6,  // block device
  FS_DIRENT_REG     = 8,  // regular file
  FS_DIRENT_LNK     = 10, // symbolic link
  FS_DIRENT_SOCK    = 12, // local-domain socket
  FS_DIRENT_WHT     = 14, // BSD whiteout
};

// fs_dirent is a directory entry
typedef struct fs_dirent {
  isize ino;       // inode number
  u8    type;      // type of file (not supported by all filesystems; 0 if unknown)
  char  name[256]; // filename (null terminated)
  u16   namlen;    // length of d_name (not including terminating null byte)
} fs_dirent;

typedef uintptr fs_dir;

error fs_dir_open(const char* filename, fs_dir* result);
error fs_dir_open_fd(int fd, fs_dir* result);
error fs_dir_close(fs_dir);
error fs_dir_read(fs_dir, fs_dirent* result); // returns 1 on success, 0 on EOF


// ======================================================================================
// Sym

// Sym is an immutable kind of string with a precomputed hash, interned in a SymPool.
// Sym is a valid null-terminated C-string.
// Sym can be compared for equality simply by comparing pointer address.
// Sym functions are tuned toward lookup rather than insertion or deletion.

typedef const char* Sym;

// SYM_FLAGS_MAX defines the largest possible flags value
#define SYM_FLAGS_MAX 31

// SYM_LEN_MAX defines the largest possible length of a symbol
#define SYM_LEN_MAX 0x7ffffff /* 0-134217727 (27-bit integer) */

// SymRBNode is a red-black tree node
typedef struct SymRBNode SymRBNode;
typedef struct SymRBNode {
  Sym                 key;
  bool                isred;
  SymRBNode* nullable left;
  SymRBNode* nullable right;
} SymRBNode;

// SymPool holds a set of syms unique to the pool
typedef struct SymPool SymPool;
typedef struct SymPool {
  SymRBNode*              root;
  const SymPool* nullable base;
  Mem                     mem;
  //rwmtx_t                 mu; // TODO MT
} SymPool;

// sympool_init initialized a SymPool
// base is an optional "parent" or "outer" read-only symbol pool to use for secondary lookups
//   when a symbol is not found in the pool.
// mem is the memory to use for SymRBNodes.
// root may be a preallocated red-black tree. Be mindful of interactions with sympool_dispose.
void sympool_init(SymPool*, const SymPool* nullable base, Mem, SymRBNode* nullable root);

// sympool_dispose frees up memory used by p (but does not free p itself)
// When a SymPool has been disposed, all symbols in it becomes invalid.
void sympool_dispose(SymPool* p);

// sympool_repr appends a printable list representation of the symbols in p to s
Str sympool_repr(const SymPool* p, Str s);

// symget "interns" a Sym in p.
// All symget functions are thread safe.
Sym symget(SymPool* p, const char* data, u32 len);

// symgetcstr is a convenience around symget for C-strings (calls strlen for you.)
static Sym symgetcstr(SymPool* p, const char* cstr);

// symfind looks up a symbol but does not add it if missing
Sym nullable symfind(SymPool* p, const char* data, u32 len);

// symadd adds a symbol to p unless it already exists in p in which case the existing
// symbol is returned.
//
// A difference between symget vs symadd is what happens when a base pool is used:
// In the case of symget the entire base pool chain is traversed looking for the symbol
// and only if that fails is a new symbol added to p.
// However with symadd p's base is not searched and a new symbol is added to p regardless
// if it exists in base pools. Additionally, the implementation of this function assumes
// that the common case is that there's no symbol for data.
Sym symadd(SymPool*, const char* data, u32 len);

// symaddcstr is a convenience around symadd for C-strings (calls strlen for you.)
static Sym symaddcstr(SymPool*, const char* cstr);

// symcmp compares two Sym's string values, like memcmp.
// Note: to check equality of syms, simply compare their addresses (e.g. a==b)
static int symcmp(Sym a, Sym b);

// symhash returns the symbol's precomputed hash
static u32 symhash(Sym);

// symlen returns a symbols precomputed string length
static u32 symlen(Sym);

// symflags returns a symbols flags.
// (currently only used for built-in keywords defined in universe)
static u8 symflags(Sym);

// ---- Sym inline implementation ----

typedef struct __attribute__((__packed__)) SymHeader {
  u32  hash;
  u32  len; // _SYM_FLAG_BITS bits flags, rest is number of bytes at p
  char p[];
} SymHeader;

#define _SYM_HEADER(s) ((const SymHeader*)((s) - (sizeof(SymHeader))))

// these work for little endian only. Sym implementation relies on LE to be able to simply
// increment the length value.
#if defined(__ARMEB__) || defined(__ppc__) || defined(__powerpc__)
#error "big-endian arch not supported"
#endif
#define _SYM_FLAG_BITS 5
// _sym_flag_mask  0b11110000...0000
// _sym_len_mask   0b00001111...1111
static const u32 _sym_flag_mask = U32_MAX ^ (U32_MAX >> _SYM_FLAG_BITS);
static const u32 _sym_len_mask  = U32_MAX ^ _sym_flag_mask;

inline static Sym symgetcstr(SymPool* p, const char* cstr) {
  return symget(p, cstr, strlen(cstr));
}

inline static Sym symaddcstr(SymPool* p, const char* cstr) {
  return symadd(p, cstr, strlen(cstr));
}

inline static int symcmp(Sym a, Sym b) { return a == b ? 0 : strcmp(a, b); }
inline static u32 symhash(Sym s) { return _SYM_HEADER(s)->hash; }
inline static u32 symlen(Sym s) { return _SYM_HEADER(s)->len & _sym_len_mask; }

inline static u8 symflags(Sym s) {
  return (_SYM_HEADER(s)->len & _sym_flag_mask) >> (32 - _SYM_FLAG_BITS);
}

// SYM_MAKELEN(u32 len, u8 flags) is a helper macro for making the "length" portion of
// the SymHeader, useful when creating Syms at compile time.
#define SYM_MAKELEN(len, flags) \
  ( ( ((u32)(flags) << (32 - _SYM_FLAG_BITS)) & _sym_flag_mask ) | ((len) & _sym_len_mask) )

// sym_dangerously_set_flags mutates a Sym by setting its flags.
// Use with caution as Syms are assumed to be constant and immutable.
inline static void sym_dangerously_set_flags(Sym s, u8 flags) {
  assert(flags <= SYM_FLAGS_MAX);
  SymHeader* h = (SymHeader*)_SYM_HEADER(s);
  h->len = ( ((u32)flags << (32 - _SYM_FLAG_BITS)) & _sym_flag_mask ) | (h->len & _sym_len_mask);
}

// sym_dangerously_set_len mutates a Sym by setting its length.
// Use with caution as Syms are assumed to be constant and immutable.
inline static void sym_dangerously_set_len(Sym s, u32 len) {
  assert(len <= symlen(s)); // can only shrink
  SymHeader* h = (SymHeader*)_SYM_HEADER(s);
  h->len = (h->len & _sym_flag_mask) | len;
  h->p[h->len] = 0;
}


// ======================================================================================
// SymMap -- hash map that maps Sym => pointer

ASSUME_NONNULL_END
#define HASHMAP_NAME  SymMap
#define HASHMAP_KEY   Sym
#define HASHMAP_VALUE void*
#include "hashmap.h"
#undef HASHMAP_NAME
#undef HASHMAP_KEY
#undef HASHMAP_VALUE
ASSUME_NONNULL_BEGIN

// SymMapInit initializes a map structure with user-provided initial bucket storage.
// initbucketsc*sizeof(SymMapBucket) bytes must be available at initbucketsv and
// initbucketsv must immediately follow in memory, i.e. at m+sizeof(SymMap).
//
// Example:
//   struct foo {
//     SymMap       m;
//     SymMapBucket mbv[8];
//   };
//   stuct foo f = {0};
//   SymMapInit(&f.m, f.mbv, countof(f.mbv), mem);
//
void SymMapInit(SymMap*, void* initbucketsv, u32 initbucketsc, Mem);

// Creates and initializes a new SymMap in mem.
SymMap* nullable SymMapNew(Mem mem, u32 initbuckets);

// SymMapFree frees a SymMap created from SymMapNew
void SymMapFree(SymMap* m);

// SymMapDispose frees SymMap memory (does not free m.)
void SymMapDispose(SymMap* m);

// SymMapLen returns the number of entries currently in the map
static u32 SymMapLen(const SymMap*);

// SymMapGet searches for key. Returns value, or NULL if not found.
void* nullable SymMapGet(const SymMap*, Sym key);

// SymMapSet inserts key=value into m.
// On return, sets *valuep_inout to a replaced value or NULL if no existing value was found.
// Returns an error if memory allocation failed during growth of the hash table.
error SymMapSet(SymMap*, Sym key, void** valuep_inout);

// SymMapDel removes value for key. Returns the removed value or NULL if not found.
void* nullable SymMapDel(SymMap*, Sym key);

// SymMapClear removes all entries. In contrast to SymMapFree, map remains valid.
void SymMapClear(SymMap*);

// Iterator function type. Set stop=true to stop iteration.
typedef void(*SymMapIterator)(Sym key, void* value, bool* stop, void* nullable userdata);

// SymMapIter iterates over entries of the map.
void SymMapIter(const SymMap*, SymMapIterator, void* nullable userdata);



// =====================================================================================
// TStyle -- TTY terminal ANSI styling

#define TSTYLE_STYLES(_) \
  /* Name               16       RGB */ \
  _(none,        "0",     "0") \
  _(nocolor,     "39",    "39") \
  _(defaultfg,   "39",    "39") \
  _(defaultbg,   "49",    "49") \
  _(bold,        "1",     "1") \
  _(dim,         "2",     "2") \
  _(nodim,       "22",    "22") \
  _(italic,      "3",     "3") \
  _(underline,   "4",     "4") \
  _(inverse,     "7",     "7") \
  _(white,       "37",    "38;2;255;255;255") \
  _(grey,        "90",    "38;5;244") \
  _(black,       "30",    "38;5;16") \
  _(blue,        "94",    "38;5;75") \
  _(lightblue,   "94",    "38;5;117") \
  _(cyan,        "96",    "38;5;87") \
  _(green,       "92",    "38;5;84") \
  _(lightgreen,  "92",    "38;5;157") \
  _(magenta,     "95",    "38;5;213") \
  _(purple,      "35",    "38;5;141") \
  _(lightpurple, "35",    "38;5;183") \
  _(pink,        "35",    "38;5;211") \
  _(red,         "91",    "38;2;255;110;80") \
  _(yellow,      "33",    "38;5;227") \
  _(lightyellow, "93",    "38;5;229") \
  _(orange,      "33",    "38;5;215") \
/*END DEF_NODE_KINDS*/

// TStyle_red, TStyle_bold, etc.
typedef enum {
  #define I_ENUM(name, c16, cRGB) TStyle_##name,
  TSTYLE_STYLES(I_ENUM)
  #undef I_ENUM
  _TStyle_MAX,
} TStyle;

extern const char* nonull TStyle16[_TStyle_MAX];
extern const char* nonull TStyleRGB[_TStyle_MAX];
extern const char* nonull TStyleNone[_TStyle_MAX];

typedef const char** TStyleTable;

bool TSTyleStdoutIsTTY();
bool TSTyleStderrIsTTY();
TStyleTable TSTyleForTerm();   // best for the current terminal
TStyleTable TSTyleForStdout(); // best for the current terminal on stdout
TStyleTable TSTyleForStderr(); // best for the current terminal on stderr

typedef struct StyleStack {
  Mem         mem;
  TStyleTable styles;
  Array       stack; // [const char*]
  const char* stack_storage[4];
  u32         nbyteswritten;
} StyleStack;

void StyleStackInit(StyleStack* sstack, Mem mem, TStyleTable styles);
void StyleStackDispose(StyleStack* sstack);
Str StyleStackPush(StyleStack* sstack, Str s, TStyle style);
Str StyleStackPop(StyleStack* sstack, Str s);



// =====================================================================================
// SHA-256

#define SHA256_CHUNK_SIZE 64

typedef struct SHA256 {
  u8*   hash;
  u8    chunk[SHA256_CHUNK_SIZE];
  u8*   chunk_pos;
  usize space_left;
  usize total_len;
  u32   h[8];
} SHA256;

void sha256_init(SHA256* state, u8 hash_storage[32]);
void sha256_write(SHA256* state, const void* data, usize len);
void sha256_close(SHA256* state);


// =====================================================================================
// Source

typedef struct Source Source;

// Source represents an input source file
struct Source {
  Source*   next;       // list link
  Str       filename;   // copy of filename given to source_open
  const u8* body;       // file body (usually mmap'ed)
  u32       len;        // size of body in bytes
  int       fd;         // file descriptor
  u8        sha256[32]; // SHA-256 checksum of body, set with source_checksum
  bool      ismmap;     // true if the file is memory-mapped
};

error source_open_file(Source* src, Mem mem, const char* filename);
error source_open_data(Source* src, Mem mem, const char* filename, const char* text, u32 len);
error source_body_open(Source* src);
error source_body_close(Source* src);
error source_close(Source* src); // src can be reused with open after this call
void  source_checksum(Source* src); // populates src->sha256 <= sha256(src->body)


// =====================================================================================
// Pkg

typedef struct Pkg Pkg;

// Pkg represents a package
struct Pkg {
  Str     id;      // fully qualified name (e.g. "bar/cat"); TODO: consider using Sym
  Source* srclist; // list of sources (linked via Source.next)
};

void pkg_add_source(Pkg* pkg, Source* src); // add src to pkg->srclist
error pkg_add_file(Pkg* pkg, Mem mem, const char* filename);
error pkg_add_dir(Pkg* pkg, Mem mem, const char* filename); // add all *.co files in dir


// =====================================================================================
// Pos

// Pos is a compact representation of a source position: source file, line and column.
// Limits: 1048575 number of sources, 1048575 max lines, 4095 max columns, 4095 max width.
// Inspired by the Go compiler's xpos & lico.
typedef u64 Pos;

// PosMap maps sources to Pos indices
typedef struct PosMap {
  Mem   mem; // used to allocate extra memory for a
  Array a;
  void* a_storage[32]; // slot 0 is always NULL
} PosMap;

// PosSpan describes a span in a source
typedef struct PosSpan {
  Pos start;
  Pos end; // inclusive, unless it's NoPos
} PosSpan;


// NoPos is a valid unknown position; pos_isknown(NoPos) returns false.
static const Pos NoPos = 0;


void posmap_init(PosMap* pm, Mem mem);
void posmap_dispose(PosMap* pm);

// posmap_origin retrieves the origin for source, allocating one if needed.
// See pos_source for the inverse function.
u32 posmap_origin(PosMap* pm, void* source);

// pos_source looks up the source for a pos. The inverse of posmap_origin.
// Returns NULL for unknown positions.
static Source* nullable pos_source(const PosMap* pm, Pos p);

static Pos pos_make(u32 origin, u32 line, u32 col, u32 width);
static Pos pos_make_unchecked(u32 origin, u32 line, u32 col, u32 width); // no bounds checks!

static u32 pos_origin(Pos p); // 0 for pos without origin
static u32 pos_line(Pos p);
static u32 pos_col(Pos p);
static u32 pos_width(Pos p);

static Pos pos_with_origin(Pos p, u32 origin); // returns copy of p with specific origin
static Pos pos_with_line(Pos p, u32 line);   // returns copy of p with specific line
static Pos pos_with_col(Pos p, u32 col);    // returns copy of p with specific col
static Pos pos_with_width(Pos p, u32 width);  // returns copy of p with specific width

// pos_with_adjusted_start returns a copy of p with its start and width adjusted by deltacol.
// Can not overflow; the result is clamped.
Pos pos_with_adjusted_start(Pos p, i32 deltacol);

// pos_union returns a Pos that covers the column extent of both a and b.
// a and b must be on the same line.
Pos pos_union(Pos a, Pos b);

// pos_isknown reports whether the position is a known position.
static bool pos_isknown(Pos);

// pos_isbefore reports whether the position p comes before q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isbefore(Pos p, Pos q);

// pos_isafter reports whether the position p comes after q in the source.
// For positions with different bases, ordering is by origin.
static bool pos_isafter(Pos p, Pos q);

// pos_str appends "file:line:col" to dst
Str pos_str(const PosMap*, Pos, Str dst);

// pos_fmt appends "file:line:col: format ..." to s, including source context
Str pos_fmt(const PosMap*, PosSpan, Str s, const char* fmt, ...) ATTR_FORMAT(printf, 4, 5);
Str pos_fmtv(const PosMap*, PosSpan, Str s, const char* fmt, va_list);

// ---- inline implementation ---

// Layout constants: 20 bits origin, 20 bits line, 12 bits column, 12 bits width.
// Limits: sources: 1048575, lines: 1048575, columns: 4095, width: 4095
// If this is too tight, we can either make lico 64b wide, or we can introduce a tiered encoding
// where we remove column information as line numbers grow bigger; similar to what gcc does.
static const u64 _pos_widthBits  = 12;
static const u64 _pos_colBits    = 12;
static const u64 _pos_lineBits   = 20;
static const u64 _pos_originBits = 64 - _pos_lineBits - _pos_colBits - _pos_widthBits;

static const u64 _pos_originMax = (1llu << _pos_originBits) - 1;
static const u64 _pos_lineMax   = (1llu << _pos_lineBits) - 1;
static const u64 _pos_colMax    = (1llu << _pos_colBits) - 1;
static const u64 _pos_widthMax  = (1llu << _pos_widthBits) - 1;

static const u64 _pos_originShift = _pos_originBits + _pos_colBits + _pos_widthBits;
static const u64 _pos_lineShift   = _pos_colBits + _pos_widthBits;
static const u64 _pos_colShift    = _pos_widthBits;

inline static Pos pos_make_unchecked(u32 origin, u32 line, u32 col, u32 width) {
  return (Pos)( ((u64)origin << _pos_originShift)
              | ((u64)line << _pos_lineShift)
              | ((u64)col << _pos_colShift)
              | width );
}

inline static Pos pos_make(u32 origin, u32 line, u32 col, u32 width) {
  return pos_make_unchecked(
    MIN(_pos_originMax, origin),
    MIN(_pos_lineMax, line),
    MIN(_pos_colMax, col),
    MIN(_pos_widthMax, width));
}

inline static u32 pos_origin(Pos p) { return p >> _pos_originShift; }
inline static u32 pos_line(Pos p)   { return (p >> _pos_lineShift) & _pos_lineMax; }
inline static u32 pos_col(Pos p)    { return (p >> _pos_colShift) & _pos_colMax; }
inline static u32 pos_width(Pos p)   { return p & _pos_widthMax; }

// TODO: improve the efficiency of these
inline static Pos pos_with_origin(Pos p, u32 origin) {
  return pos_make_unchecked(MIN(_pos_originMax, origin), pos_line(p), pos_col(p), pos_width(p));
}
inline static Pos pos_with_line(Pos p, u32 line) {
  return pos_make_unchecked(pos_origin(p), MIN(_pos_lineMax, line), pos_col(p), pos_width(p));
}
inline static Pos pos_with_col(Pos p, u32 col) {
  return pos_make_unchecked(pos_origin(p), pos_line(p), MIN(_pos_colMax, col), pos_width(p));
}
inline static Pos pos_with_width(Pos p, u32 width) {
  return pos_make_unchecked(pos_origin(p), pos_line(p), pos_col(p), MIN(_pos_widthMax, width));
}

inline static bool pos_isbefore(Pos p, Pos q) { return p < q; }
inline static bool pos_isafter(Pos p, Pos q) { return p > q; }

inline static bool pos_isknown(Pos p) {
  return pos_origin(p) != 0 || pos_line(p) != 0;
}

inline static Source* nullable pos_source(const PosMap* pm, Pos p) {
  return (Source*)pm->a.v[pos_origin(p)];
}


// ======================================================================================
ASSUME_NONNULL_END
