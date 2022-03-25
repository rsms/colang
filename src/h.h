// header included by "unified interface and implementation" files
#ifndef CO_IMPL
  #include "coimpl.h"
  #define IMPLEMENTATION
#else
  #if defined(IMPLEMENTATION)
    #warning IMPLEMENTATION is defined
  #endif
  #undef IMPLEMENTATION
#endif

#define BEGIN_INTERFACE \
  ASSUME_NONNULL_BEGIN \
  DIAGNOSTIC_IGNORE_PUSH("-Wunused-function")

#define END_INTERFACE \
  DIAGNOSTIC_IGNORE_POP() \
  ASSUME_NONNULL_END

// #define BEGIN_IMPLEMENTATION \
//   ASSUME_NONNULL_BEGIN

// #define END_IMPLEMENTATION \
//   ASSUME_NONNULL_END
