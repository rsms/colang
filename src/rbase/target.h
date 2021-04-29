// Copyright (c) 2012-2021 Rasmus Andersson <http://rsms.me/> (See LICENSE)
//
// This file defines a set of preprocessor values related to the build target.
//
// R_TARGET_ARCH_NAME         "?" | "arm" | "arm64" | "x64" | "x86"
// R_TARGET_ARCH_SIZE         8 | 32 | 64
// R_TARGET_ARCH_ARM          bool  // true for both 32 and 64 bit arm
// R_TARGET_ARCH_ARM64        bool
// R_TARGET_ARCH_LE           bool
// R_TARGET_ARCH_MIPS         bool
// R_TARGET_ARCH_PPC          bool
// R_TARGET_ARCH_PPCSPE       bool
// R_TARGET_ARCH_X86_64       bool
// R_TARGET_ARCH_386          bool
// R_TARGET_ARCH_X86          bool  // true for both ARCH_386 and ARCH_X86_64
//
// R_TARGET_OS_NAME           "?" | "bsd" | "darwin" | "ios" | "ios-simulator"
//                            | "linux" | "osx" | "posix" | "win32"
// R_TARGET_OS_BSD            bool
// R_TARGET_OS_DARWIN         bool
// R_TARGET_OS_IOS            bool
// R_TARGET_OS_IOS_SIMULATOR  bool
// R_TARGET_OS_LINUX          bool
// R_TARGET_OS_OSX            bool
// R_TARGET_OS_WINDOWS        bool
// R_TARGET_OS_POSIX          bool   // 1 for all posix OSes
// R_TARGET_OS_UNKNOWN        bool   // 1 if the OS could not be detected
//
// R_TARGET_CXX_EXCEPTIONS    bool
// R_TARGET_CXX_EXCEPTIONS    bool
// R_TARGET_CXX_RTTI          bool
//
#pragma once

//-- begin R_TARGET_ARCH_*
#if defined(__i386) || defined(__i386__) || defined(_M_IX86)
  #define R_TARGET_ARCH_X86 1
  #define R_TARGET_ARCH_386 1
#elif defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || \
      defined(_M_AMD64)
  #define R_TARGET_ARCH_X86 1
  #define R_TARGET_ARCH_X86_64 1
#elif defined(__arm__) || defined(__arm) || defined(__ARM__) || defined(__ARM) \
      || defined(__arm64__) || defined(__aarch64__)
  #define R_TARGET_ARCH_ARM 1
  #if defined(__arm64__) || defined(__aarch64__)
    #define R_TARGET_ARCH_ARM64 1
  #endif
#elif defined(__ppc__) || defined(__ppc) || defined(__PPC__) || \
      defined(__PPC) || defined(__powerpc__) || defined(__powerpc) || \
      defined(__POWERPC__) || defined(__POWERPC) || defined(_M_PPC)
  #define R_TARGET_ARCH_PPC 1
  // TODO PPC64
  #ifdef __NO_FPRS__
    #define R_TARGET_ARCH_PPCSPE 1
  #endif
#elif defined(__mips__) || defined(__mips) || defined(__MIPS__) || \
      defined(__MIPS)
  // TODO MIPS64
  #define R_TARGET_ARCH_MIPS 1
#else
  #error "Unsupported target architecture"
#endif

#if R_TARGET_ARCH_X86_64
  #define R_TARGET_ARCH_NAME     "x86_64"
  #define R_TARGET_ARCH_SIZE     64
  #define R_TARGET_ARCH_LE       1
#elif R_TARGET_ARCH_X86
  #define R_TARGET_ARCH_NAME     "386"
  #define R_TARGET_ARCH_SIZE     32
  #define R_TARGET_ARCH_LE       1
#elif R_TARGET_ARCH_ARM
  #if defined(__ARMEB__)
    #error "Unsupported target architecture: Big endian ARM"
  #endif
  #define R_TARGET_ARCH_LE       1
  #if R_TARGET_ARCH_ARM64
    #define R_TARGET_ARCH_NAME     "arm64"
    #define R_TARGET_ARCH_SIZE     64
  #else
    #define R_TARGET_ARCH_NAME     "arm"
    #define R_TARGET_ARCH_SIZE     32
  #endif
#elif R_TARGET_ARCH_PPC
  // TODO PPC64
  #define R_TARGET_ARCH_NAME     "ppc"
  #define R_TARGET_ARCH_SIZE     32
  #define R_TARGET_ARCH_LE       0
#elif R_TARGET_ARCH_MIPS
  // TODO MIPS64
  #define R_TARGET_ARCH_NAME     "mips"
  #define R_TARGET_ARCH_SIZE     32
  #define R_TARGET_ARCH_LE       1 /* really? */
#else
  #define R_TARGET_ARCH_NAME     "?"
  #define R_TARGET_ARCH_SIZE     8
  #define R_TARGET_ARCH_LE       0
#endif
//-- end R_TARGET_ARCH_*

//-- begin R_TARGET_OS_*
#if (defined(WIN32) || defined(_WIN32)) && !defined(_XBOX_VER)
  #define R_TARGET_OS_WINDOWS 1
  #define R_TARGET_OS_NAME "win32"
#elif defined(__linux__)
  #define R_TARGET_OS_LINUX 1
  #define R_TARGET_OS_POSIX 1
  #define R_TARGET_OS_NAME "linux"
#elif defined(__MACH__) && defined(__APPLE__)
  #include <TargetConditionals.h>
  #define R_TARGET_OS_DARWIN 1
  #define R_TARGET_OS_POSIX 1
  #if TARGET_OS_IPHONE
    #define R_TARGET_OS_IOS 1
    #if TARGET_IPHONE_SIMULATOR
      #define R_TARGET_OS_NAME "ios-simulator"
      #define R_TARGET_OS_IOS_SIMULATOR 1
    #else
      #define R_TARGET_OS_NAME "ios"
      #define R_TARGET_OS_IOS_SIMULATOR 0
    #endif
  #elif TARGET_OS_MAC
    #define R_TARGET_OS_OSX 1
    #define R_TARGET_OS_NAME "osx"
  #else
    #define R_TARGET_OS_NAME "darwin"
  #endif
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || \
      defined(__NetBSD__) || defined(__OpenBSD__)
  #define R_TARGET_OS_BSD 1
  #define R_TARGET_OS_POSIX 1
  #define R_TARGET_OS_NAME "bsd"
#elif (defined(__sun__) && defined(__svr4__)) || defined(__solaris__) || \
      defined(__CYGWIN__)
  #define R_TARGET_OS_POSIX 1
  #define R_TARGET_OS_NAME "posix"
#else
  #define R_TARGET_OS_UNKNOWN 1
  #define R_TARGET_OS_NAME "?"
#endif
//-- end R_TARGET_OS_*

//-- begin R_TARGET_CXX_*
#if !defined(__GXX_RTTI) || !__GXX_RTTI
  #define R_TARGET_CXX_RTTI 0
#else
  #define R_TARGET_CXX_RTTI 1
#endif

#if R_TARGET_OS_WINDOWS && !defined(_CPPUNWIND)
  #define R_TARGET_CXX_EXCEPTIONS 0
#elif !defined(__EXCEPTIONS) || !__EXCEPTIONS
  #define R_TARGET_CXX_EXCEPTIONS 0
#else
  #define R_TARGET_CXX_EXCEPTIONS 1
#endif
//-- end R_TARGET_CXX_*

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
