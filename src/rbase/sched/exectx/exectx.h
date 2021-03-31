#pragma once

typedef struct exectx_t {
  void* ctx;
  void* data;
} exectx_t;

// exectx_init initializes memory ending at sp (stack base pointer) by setting up a call to fn.
// The size argument should be the total size of memory below sp. This function will only use
// a small portion of the memory below sp (64 bytes on x86_64.)
// Calling exectx_switch or exectx_jump with the returned handle will execute fn.
void* exectx_init(void* sp, size_t size, void(*fn)(exectx_t));

// transfer_t ontop_fcontext( fcontext_t const to, void * vp, transfer_t (* fn)( transfer_t) );

// exectx_switch saves the current (caller) execution context on the caller stack, then
// restores registers from ctx and finally jumps to wherever ctx left off (or fn from exectx_init
// if switch has not been called before for this ctx.)
// Effectively it both does exectx_save and exectx_jump
exectx_t exectx_switch(void* const ctx, void* data);

// exectx_jump restores exection at ctx, replacing the current exection context
exectx_t noreturn exectx_jump(void* const ctx, void* data);
