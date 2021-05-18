//
// This files contains definitions used across the entire codebase.
// Keep it lean.
//
#pragma once
#include <rbase/rbase.h>

// Outline of memory functions:
//
// Mem MemHeap            A shared thread-safe heap allocator
// type MemArena          MemArena is the type of a memory arena
// Mem MemArenaAlloc()    Allocate a new MemArena
// void MemArenaFree(Mem) Frees all memory allocated in the arena
//
#define CO_MEM_USE_JEMALLOC /* use jemalloc instead of libc */
#ifdef CO_MEM_USE_JEMALLOC
  #include <jemalloc-mem.h>
  #define MemHeap MemJEMalloc()
  #if 1 /* use jemalloc arenas instead of rbase/MemArenaShim */
    typedef Mem MemArena;
    inline static Mem MemArenaAlloc() {
      return MemJEMallocArenaAlloc(MemJEMallocArenaDummyFree);
    }
    inline static void MemArenaFree(Mem m) {
      MemJEMallocArenaFree(m);
    }
  #else
    typedef MemArenaShim MemArena;
    inline static Mem MemArenaAlloc() {
      auto a = memalloct(MemHeap, MemArena);
      return MemArenaShimInit(a, MemHeap);
    }
    inline static void MemArenaFree(Mem m) {
      auto a = (MemArenaShim*)m;
      MemArenaShimFree(a);
      memfree(MemHeap, a);
    }
  #endif
#else
  #define MemHeap MemLibC()
  typedef MemArenaShim MemArena;
  inline static Mem MemArenaAlloc() {
    auto a = memalloct(MemHeap, MemArena);
    return MemArenaShimInit(a, MemHeap);
  }
  inline static void MemArenaFree(Mem m) {
    auto a = (MemArenaShim*)m;
    MemArenaShimFree(a);
    memfree(MemHeap, a);
  }
#endif
