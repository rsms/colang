#pragma once
typedef void* fctx_t;

typedef struct fctx_transfer_t {
  fctx_t ctx;
  void*  data;
} fctx_transfer_t;

// sp = top of stack
fctx_t make_fcontext(void* sp, size_t size, void(*fn)(fctx_transfer_t));
fctx_transfer_t jump_fcontext(fctx_t const to, void* vp);
