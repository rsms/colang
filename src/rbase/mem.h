#pragma once
ASSUME_NONNULL_BEGIN

// Mem is an isolated-space memory allocator, useful for allocating many small
// short-lived fragments of memory, like for example AST nodes.
//
// Passing NULL to mangagement functions like memalloc uses a shared global allocator
// and works the same way as libc malloc, free et al.
//
typedef void* Mem;

// mem_pagesize returns the system's memory page size, which is usually 4096 bytes.
// This function returns a cached value from memory, read from the OS at init.
size_t mem_pagesize();

// memalloc allocates memory. Returned memory is zeroed.
static void* memalloc(Mem nullable mem, size_t size) nonnullreturn;

// memalloc_raw allocates memory but does not initialize it.
static void* memalloc_raw(Mem nullable mem, size_t size) nonnullreturn;

// memalloct is a convenience for: (MyStructType*)memalloc(m, sizeof(MyStructType))
#define memalloct(mem, TYPE) ((TYPE*)memalloc(mem, sizeof(TYPE)))

// memalloc reallocates some memory. Additional memory is NOT zeroed.
static void* memrealloc(Mem nullable mem, void* nullable ptr, size_t newsize) nonnullreturn;

// memalloc_aligned returns a pointer to a newly allocated chunk of n bytes, aligned
// in accord with the alignment argument.
//
// The alignment argument should be a power of two. If the argument is not a power of two,
// the nearest greater power is used. 8-byte alignment is guaranteed by normal memalloc calls,
// so don't bother calling memalloc_aligned with an argument of 8 or less.
//
// Warning: Overreliance on memalloc_aligned is a sure way to fragment space.
static void* memalloc_aligned(Mem nullable mem, size_t alignment, size_t bytes);

// memfree frees memory.
static void memfree(Mem nullable mem, void* nonull ptr);

// memstrdup is like strdup but uses mem
static char* memstrdup(Mem nullable mem, const char* nonull pch);

// memstrdupcat concatenates up to 20 c-strings together.
// Arguments must be terminated with NULL.
char* memstrdupcat(Mem nullable mem, const char* nonull s1, ...);

// memsprintf is like sprintf but uses memory from mem.
char* memsprintf(Mem mem, const char* format, ...);

// memdup makes a copy of src
static void* memdup(Mem mem, const void* src, size_t len);

// memdup2 is like memdup but takes an additional arg extraspace for allocating additional
// uninitialized space after len.
void* memdup2(Mem mem, const void* src, size_t len, size_t extraspace);

// Memory spaces (arenas)
Mem MemNew(size_t initcap /* 0 = pagesize */);
void MemRecycle(Mem* memptr); // recycle for reuse
void MemFree(Mem mem);        // free all memory allocated by mem

// -----------------------------------------------------------------------------------------------

extern Mem _gmem; // the global memory space

void  mspace_free(Mem msp, void* mem);
void* mspace_calloc(Mem msp, size_t n_elements, size_t elem_size);
void* mspace_malloc(Mem msp, size_t nbytes);
void* mspace_realloc(Mem msp, void* mem, size_t newsize);
void* mspace_memalign(Mem msp, size_t alignment, size_t bytes);

inline static void* memdup(Mem mem, const void* src, size_t len) {
  return memdup2(mem, src, len, 0);
}

inline static void* memalloc(Mem nullable mem, size_t size) {
  return mspace_calloc(mem == NULL ? _gmem : mem, 1, size);
}

inline static void* memalloc_raw(Mem nullable mem, size_t size) {
  return mspace_malloc(mem == NULL ? _gmem : mem, size);
}

inline static void* memrealloc(Mem nullable mem, void* nullable ptr, size_t newsize) {
  return mspace_realloc(mem == NULL ? _gmem : mem, ptr, newsize);
}

inline static void* memalloc_aligned(Mem nullable mem, size_t alignment, size_t bytes) {
  return mspace_memalign(mem == NULL ? _gmem : mem, alignment, bytes);
}

inline static void memfree(Mem nullable mem, void* ptr) {
  mspace_free(mem == NULL ? _gmem : mem, ptr);
}

inline static char* memstrdup(Mem nullable mem, const char* pch) {
  size_t z = len(pch);
  char* s = (char*)memdup2(mem, pch, z, 1);
  s[z] = 0;
  return s;
}

ASSUME_NONNULL_END
