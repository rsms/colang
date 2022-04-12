// panic, assert, debug logging
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Rasmus Andersson. See accompanying LICENSE file for details.
//
#pragma once
BEGIN_INTERFACE

// panic prints msg to stderr and calls abort()
#define panic(fmt, args...) _panic(__FILE__, __LINE__, __FUNCTION__, fmt, ##args)

EXTERN_C NORETURN ATTR_FORMAT(printf, 4, 5)
void _panic(const char* file, int line, const char* fun, const char* fmt, ...);

// void log(const char* fmt, ...)
#ifdef CO_NO_LIBC
  #warning log not implemented for no-libc
  #define log(format, ...) ((void)0)
#else
  ASSUME_NONNULL_END
  #include <stdio.h>
  ASSUME_NONNULL_BEGIN
  #define log(format, args...) fprintf(stderr, format "\n", ##args)
#endif

// void errlog(const char* fmt, ...)
#define errlog(format, args...) ({                              \
  log("error: " format " (%s:%d)", ##args, __FILE__, __LINE__); \
  fflush(stderr); })

// void assert(expr condition)
#undef assert
#if defined(DEBUG) || !defined(NDEBUG)
  #undef DEBUG
  #undef NDEBUG
  #undef CO_SAFE
  #define DEBUG 1
  #define CO_SAFE 1

  #define _assertfail(fmt, args...) \
    _panic(__FILE__, __LINE__, __FUNCTION__, "Assertion failed: " fmt, args)
  // Note: we can't use ", ##args" above in either clang nor gcc for some reason,
  // or else certain applications of this macro are not expanded.

  #define assertf(cond, fmt, args...) \
    (UNLIKELY(!(cond)) ? _assertfail(fmt " (%s)", ##args, #cond) : ((void)0))

  #define assert(cond) \
    (UNLIKELY(!(cond)) ? _assertfail("%s", #cond) : ((void)0))

  #define assertop(a,op,b) ({                                            \
    __typeof__(a) A__ = (a);                                             \
    __typeof__(a) B__ = (b);                                             \
    if (UNLIKELY(!(A__ op B__)))                                         \
      _assertfail("%s %s %s (%s %s %s)",                                 \
        #a, #op, #b, debug_quickfmt(0,A__), #op, debug_quickfmt(1,B__)); \
  })

  #define assertcstreq(cstr1, cstr2) ({                  \
    const char* cstr1__ = (cstr1);                       \
    const char* cstr2__ = (cstr2);                       \
    if (UNLIKELY(strcmp(cstr1__, cstr2__) != 0))         \
      _assertfail("\"%s\" != \"%s\"", cstr1__, cstr2__); \
  })

  #define asserteq(a,b)    assertop((a),==,(b))
  #define assertne(a,b)    assertop((a),!=,(b))
  #define assertlt(a,b)    assertop((a),<, (b))
  #define assertgt(a,b)    assertop((a),>, (b))
  #define assertnull(a)    assertop((a),==,NULL)

  #define assertnotnull(a) ({                                         \
    __typeof__(a) val__ = (a);                                        \
    UNUSED const void* valp__ = val__; /* build bug on non-pointer */ \
    if (UNLIKELY(val__ == NULL))                                      \
      _assertfail("%s != NULL", #a);                                  \
    val__; })

#else /* !defined(NDEBUG) */
  #undef DEBUG
  #undef NDEBUG
  #define NDEBUG 1
  #define assert(cond)            ((void)0)
  #define assertf(cond, fmt, ...) ((void)0)
  #define assertop(a,op,b)        ((void)0)
  #define assertcstreq(a,b)       ((void)0)
  #define asserteq(a,b)           ((void)0)
  #define assertne(a,b)           ((void)0)
  #define assertlt(a,b)           ((void)0)
  #define assertgt(a,b)           ((void)0)
  #define assertnull(a)           ((void)0)
  #define assertnotnull(a)        ({ a; }) /* note: (a) causes "unused" warnings */
#endif /* !defined(NDEBUG) */


// CO_SAFE -- checks enabled in "debug" and "safe" builds (but not in "fast" builds.)
//
// void safecheck(COND)                        -- elided from non-safe builds
// void safecheckf(COND, const char* fmt, ...) -- elided from non-safe builds
// EXPR safecheckexpr(EXPR, EXPECT)            -- included in non-safe builds (without check)
// typeof(EXPR) safenotnull(EXPR)              -- included in non-safe builds (without check)
//
#ifdef CO_SAFE
  #undef CO_SAFE
  #define CO_SAFE 1

  #define _safefail(fmt, args...) \
    _panic(__FILE__, __LINE__, __FUNCTION__, fmt, ##args)

  #define safecheckf(cond, fmt, args...) \
    ( UNLIKELY(!(cond)) ? _safefail(fmt, ##args) : ((void)0) )

  #define safecheck(cond) \
    ( UNLIKELY(!(cond)) ? _safefail("safecheck (%s)", #cond) : ((void)0) )

  #define safecheckalloc(ptr) \
    ({ __typeof__(ptr) ptr__=(ptr); \
      UNUSED const void* ptr1__=ptr__; /* bug on non-ptr type */ \
      if UNLIKELY(!ptr__) _safefail("out of memory"); \
      ptr__; })

  #ifdef DEBUG
    #define safecheckexpr(expr, expect) ({                                        \
      __typeof__(expr) val__ = (expr);                                            \
      safecheckf(val__ == expect, "unexpected value (%s != %s)", #expr, #expect); \
      val__; })

    #define safenotnull(a) ({                                           \
      __typeof__(a) val__ = (a);                                        \
      UNUSED const void* valp__ = val__; /* build bug on non-pointer */ \
      safecheckf(val__ != NULL, "unexpected NULL (%s)", #a);            \
      val__; })
  #else
    #define safecheckexpr(expr, expect) ({ \
      __typeof__(expr) val__ = (expr); safecheck(val__ == expect); val__; })

    #define safenotnull(a) ({                                           \
      __typeof__(a) val__ = (a);                                        \
      UNUSED const void* valp__ = val__; /* build bug on non-pointer */ \
      safecheckf(val__ != NULL, "NULL");                                \
      val__; })
  #endif
#else
  #define safecheckf(cond, fmt, args...) ((void)0)
  #define safecheck(cond)                ((void)0)
  #define safecheckexpr(expr, expect)    (expr) /* intentionally complain if not used */
  #define safenotnull(a)                 ({ a; }) /* note: (a) causes "unused" warnings */
  #define safecheckalloc(ptr)            (ptr)
#endif

// void dlog(const char* fmt, ...)
#ifdef DEBUG
  // debug_quickfmt formats a value x and returns a temporary string for use in printing.
  // The buffer argument should be a number in the inclusive range [0-5], determining which
  // temporary buffer to use and return a pointer to.
  #define debug_quickfmt(buffer, x) debug_tmpsprintf(buffer, _Generic((x), \
    unsigned long long: "%llu", \
    unsigned long:      "%lu", \
    unsigned int:       "%u", \
    unsigned short:     "%u", \
    long long:          "%lld", \
    long:               "%ld", \
    int:                "%d", \
    short:              "%d", \
    char:               "%c", \
    unsigned char:      "%C", \
    const char*:        "%s", \
    char*:              "%s", \
    bool:               "%d", \
    float:              "%f", \
    double:             "%f", \
    void*:              "%p", \
    const void*:        "%p", \
    default:            "%lld" \
  ), (x))
  // debug_tmpsprintf is like sprintf but uses a static buffer.
  // The buffer argument determines which buffer to use (constraint: buffer<6)
  EXTERN_C const char* debug_tmpsprintf(int buffer, const char* fmt, ...);
  // intentionally no ATTR_FORMAT
  #ifdef CO_NO_LIBC
    #define dlog(format, args...) \
      log("[D] " format " (%s:%d)", ##args, __FILE__, __LINE__)
  #else
    ASSUME_NONNULL_END
    #include <unistd.h> // isatty
    ASSUME_NONNULL_BEGIN
    #define dlog(format, args...) ({                                 \
      if (isatty(2)) log("\e[1;35m▍\e[0m" format " \e[2m%s:%d\e[0m", \
                         ##args, __FILE__, __LINE__);                \
      else           log("[D] " format " (%s:%d)",                   \
                         ##args, __FILE__, __LINE__);                \
      fflush(stderr); })
  #endif
#else
  #define dlog(format, ...) ((void)0)
#endif

END_INTERFACE
