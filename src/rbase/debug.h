#pragma once
//
// void dlog(const char* fmt, ...)
// void assert(bool cond)
// void assertf(bool cond, const char* fmt, ...)
// void assertop<T>(T a, OP, T b) -- like assert(a OP b) but with better error message
// void asserteq<T>(T a, T b) -- assert(a == b)
// void assertne<T>(T a, T b) -- assert(a != b)
// void assertnull(a) -- assert(a == NULL)
// T assertnotnull<T>(a) -- ({ assert(a != NULL); a; })
//
// const char* debug_quickfmt<T>(int buffer in 0|1|2|3|4|5, T x)
// const char* debug_tmpsprintf(int buffer in 0|1|2|3|4|5, const char* fmt, ...)
//
ASSUME_NONNULL_BEGIN

#ifdef DEBUG
  #define dlog(fmt, ...) \
    fprintf(stderr, "\e[1m" fmt " \e[0;2m(%s)\e[0m\n", ##__VA_ARGS__, __FUNCTION__)
#else
  #define dlog(fmt, ...) do{}while(0)
#endif


#if !defined(NDEBUG)
  // TODO: see wlang/src/common/assert.c
  #ifdef assert
    #undef assert
  #endif

  #define assert(cond) do{ \
    if (R_UNLIKELY(!(cond))) panic("Assertion failed: %s", #cond); \
  }while(0)

  #define assertf(cond, fmt, ...) do{ \
    if (R_UNLIKELY(!(cond))) panic("Assertion failed: %s; " fmt, #cond, ##__VA_ARGS__); \
  }while(0)

  #define assertop(a,op,b) ({                                             \
    __typeof__(a) A = a;                                                  \
    __typeof__(a) B = b; /* intentionally typeof(a) and not b for lits */ \
    if (R_UNLIKELY(!(A op B)))                                            \
      panic("Assertion failed: %s %s %s (%s %s %s)",                      \
            #a, #op, #b, debug_quickfmt(0,A), #op, debug_quickfmt(1,B));  \
  })

  #define asserteq(a,b)    assertop((a),==,(b))
  #define assertne(a,b)    assertop((a),!=,(b))
  #define assertnull(a)    assertop((a),==,NULL)
  #define assertnotnull(a) ({ __typeof__(a) v = (a); assertop((v),!=,NULL); v; })

#else /* !defined(NDEBUG) */
  #ifndef assert
    #define assert(cond) do{}while(0)
  #endif
  #define assertf(cond, fmt, ...) do{}while(0)
  #define assertop(a,op,b)        do{}while(0)
  #define asserteq(a,b)           do{}while(0)
  #define assertne(a,b)           do{}while(0)
  #define assertnull(a)           do{}while(0)
  #define assertnotnull(a)        (a)
#endif /* !defined(NDEBUG) */


#ifdef DEBUG

// debug_quickfmt formats a value x and returns a temporary string for use in printing.
// The buffer argument should be a number in the inclusive range [0-5], determining which
// temporary buffer to use and return a pointer to.
#define debug_quickfmt(buffer, x) debug_tmpsprintf(buffer, _Generic((x), \
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
  bool:               "%d", \
  float:              "%f", \
  double:             "%f", \
  void*:              "%p", \
  const void*:        "%p", \
  default:            "%p" \
), x)

// debug_tmpsprintf is like sprintf but uses a static buffer.
// The buffer argument determines which buffer to use and must be in the inclusive range [0-5]
const char* debug_tmpsprintf(int buffer, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);

#else /* if !defined(DEBUG) */
#define debug_quickfmt(...) "DEBUG DISABLED"
#define debug_tmpsprintf(...) "DEBUG DISABLED"
#endif /* defined(DEBUG) */


ASSUME_NONNULL_END
