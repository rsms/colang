#pragma once

// STK_ARRAY is a simple array allocated on the stack if the size needed at runtime
// is less or equal to the memory allocated on stack, else uses heap memory.
//
// STK_ARRAY_DEFINE(myarray,int, /* comptime on-stack count: */ 8);
// STK_ARRAY_INIT(myarray,mem, /* runtime min count: */ needcount);
// myarray[needcount - 1] = lastitem;
// STK_ARRAY_DISPOSE(myarray);
//
#define STK_ARRAY_DEFINE(NAME,T,STKCAP) \
  T NAME##_stk_[STKCAP];                \
  T* NAME = NAME##_stk_;                \
  Mem NAME##_mem_ = NULL

#define STK_ARRAY_INIT(NAME,mem,MINLEN)                        \
  if ((MINLEN) > countof(NAME##_stk_)) {                       \
    NAME##_mem_ = mem;                                         \
    NAME = memalloc((mem), sizeof(NAME##_stk_[0]) * (MINLEN)); \
  }

#define STK_ARRAY_MAKE(NAME,mem,T,STKCAP,MINLEN) \
  STK_ARRAY_DEFINE(NAME,T,STKCAP);               \
  STK_ARRAY_INIT(NAME,mem,MINLEN)

#define STK_ARRAY_DISPOSE(NAME) do {               \
  if (NAME##_mem_) { memfree(NAME##_mem_, NAME); } \
} while(0)
