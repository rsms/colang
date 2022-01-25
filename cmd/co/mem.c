#include "coimpl.h"

void* mem_dup2(Mem mem, const void* src, usize len, usize extraspace) {
  assert(mem != NULL);
  assert(src != NULL);
  void* dst = mem->alloc(mem, len + extraspace);
  if (!dst)
    return NULL;
  return memcpy(dst, src, len);
}

char* mem_strdup(Mem mem, const char* cstr) {
  assert(cstr != NULL);
  usize z = strlen(cstr);
  char* s = (char*)mem_dup2(mem, cstr, z, 1);
  if (s)
    s[z] = 0;
  return s;
}

// MemBufAllocator

static void* nullable _mem_buf_alloc(Mem m, usize size) {
  MemBufAllocator* a = (MemBufAllocator*)m;
  if (a->cap - a->len < size)
    return NULL;
  void* p = &a->buf[a->len];
  a->len += size;
  return p;
}

static void* nullable _mem_buf_realloc(Mem m, void* nullable ptr, usize newsize) {
  // MemBufAllocator* a = (MemBufAllocator*)m;
  // return realloc(ptr, newsize);
  panic("TODO");
  return NULL;
}

static void _mem_buf_free(Mem m, void* nonull ptr) {
  // MemBufAllocator* a = (MemBufAllocator*)m;
  dlog("TODO");
}

Mem mem_buf_allocator_init(MemBufAllocator* a, void* buf, usize size) {
  a->a.alloc = _mem_buf_alloc;
  a->a.realloc = _mem_buf_realloc;
  a->a.free = _mem_buf_free;
  a->buf = buf;
  a->cap = size;
  a->len = 0;
  return (Mem)a;
}
