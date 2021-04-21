#pragma once
//
// void dlog(const char* fmt, ...)
// void assert(bool cond)
// void asserteq<T>(T a, T b)
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
    if (!(cond)) panic("Assertion failed: %s", #cond); \
  }while(0)

  #define assertop(a,op,b) ({                                             \
    __typeof__(a) A = a;                                                  \
    __typeof__(b) B = b;                                                  \
    if (!(A op B))                                                        \
      panic("Assertion failed: %s %s %s (%s %s %s)",                      \
            #a, #op, #b, debug_quickfmt(0,A), #op, debug_quickfmt(1,B));  \
  })

  #define asserteq(a,b) assertop(a,==,b)

#else /* !defined(NDEBUG) */
  #ifndef assert
    #define assert(cond) do{}while(0)
  #endif
  #define asserteq(a,b) do{}while(0)
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
