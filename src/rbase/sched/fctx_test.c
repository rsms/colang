#include "../rbase.h"
#include "fctx.h"
// #include <setjmp.h>
#include <signal.h>

#define USE_MMAP
#define USE_MPROTECT

#ifdef USE_MMAP
  #include <sys/types.h>
  #include <sys/mman.h>
  #if defined(__MACH__) && defined(__APPLE__)
    #include <mach/vm_statistics.h>
    #include <mach/vm_prot.h>
  #endif
  #ifndef MAP_ANON
    #define MAP_ANON MAP_ANONYMOUS
  #endif
#else
  #undef USE_MPROTECT
#endif


// [from scheschedimpl.h]
// STACK_ALIGN is the required alignment of the SP register.
// The stack must be at least word aligned, but some architectures require more.
// The values comes from go runtime/internal/sys/arch_*.go
#if R_TARGET_ARCH_ARM64 /* || TODO: R_TARGET_ARCH_PPC64 in target.h */
  #define STACK_ALIGN  16
#else
  // 386, x86_64,
  #define STACK_ALIGN  sizeof(void*)
#endif


typedef enum TStatus {
  TIdle = 0,
  TRunning,
  TWaiting,
  TYielding, // like TWaiting, but immediately adds the task to the end of the run queue
  TDead,
} TStatus;

typedef struct T T;
typedef struct T {
  void(*fn)(T*);
  TStatus status;
  fctx_t  parentctx;  // parent execution context
  fctx_t  stackctx;  // own execution context
  u8*     stackhi;  // stack base (high address)
  u8*     stacklo;  // stack top (low address)
  T*      schedlink; // runq link
} __attribute__((aligned (STACK_ALIGN))) T;

#define STACK_SIZE(t) \
  ((size_t)((uintptr_t)(t)->stackhi - (uintptr_t)(t)->stacklo))

static void init_sighandler();


static void t_free_stack(T* t) {
  munmap(t->stacklo, STACK_SIZE(t));
  t->stacklo = NULL;
  t->stackhi = NULL;
  t->stackctx = NULL;
}

static size_t stack_guard_size =
  #ifdef USE_MPROTECT
    0xFFFFFFFF; // marker that it should be page size
  #else
    0;
  #endif

static u8* t_alloc_stack(size_t npages, size_t* stacksize_out) {
  // read system page size
  size_t pagesize = mem_pagesize();
  if (stack_guard_size == 0xFFFFFFFF) {
    stack_guard_size = pagesize;
  }

  // stacksize
  // #define STACK_SIZE  4096
  // size_t stacksize = POW2_CEIL(MAX(STACK_SIZE & -16, sizeof(void*)));
  size_t stacksize = pagesize * npages;
  #ifdef USE_MPROTECT
    // additional page to use for stack protection
    stacksize += stack_guard_size;
  #endif
  dlog("t_new: allocating stack of size %zu", stacksize);

  // allocate stack memory
  // TODO: For Windows, use VirtualAlloc
  //       See https://www.mikemarcin.com/post/coroutine_a_million_stacks/
  //
  #ifdef USE_MMAP
    int fd = -1;
    int prot = PROT_READ|PROT_WRITE;
    int flags = MAP_PRIVATE|MAP_ANON;

    #ifdef MAP_NOCACHE
    flags |= MAP_NOCACHE; // don't cache pages for this mapping
    #endif

    #ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE; // don't reserve needed swap area
    #endif

    #if R_TARGET_OS_DARWIN && defined(VM_FLAGS_PURGABLE)
      fd = VM_FLAGS_PURGABLE; // Create a purgable VM object for that new VM region.
    #endif

    #if R_TARGET_OS_DARWIN && defined(VM_PROT_DEFAULT)
      // vm_map_entry_is_reusable uses VM_PROT_DEFAULT as a condition for page reuse.
      // See http://fxr.watson.org/fxr/source/osfmk/vm/vm_map.c?v=xnu-2050.18.24#L10705
      prot = VM_PROT_DEFAULT;
    #endif

    u8* lo = mmap(0, stacksize, prot, flags, fd, 0);
    if (lo == MAP_FAILED)
      return NULL;

    #ifdef USE_MPROTECT
      if (mprotect(lo, stack_guard_size, PROT_NONE) != 0) {
        munmap(lo, stacksize);
        return NULL;
      }
    #endif
  #endif // defined(USE_MMAP)

  *stacksize_out = stacksize;
  return lo;
}


static void noreturn NO_INLINE t_main(fctx_transfer_t tr) {
  T* t = tr.data;
  t->parentctx = tr.ctx;
  t->fn(t);
  t->status = TDead;
  dlog("t_main: task %p ended", t);
  jump_fcontext(tr.ctx, NULL);
  UNREACHABLE;
  #undef t
}


static T* t_new() {
  // stack layout
  //
  // 0x0000  end of stack ("lo")
  //   guard page (1 page long)
  // 0x1000  end of program stack
  //   ...
  //   program data
  //   ...
  // 0x1FD0  beginning of program stack
  //   storage for T
  // 0x2000  beginning of stack ("hi")
  //
  size_t stacksize;
  u8* stacklo = t_alloc_stack(2 /* pages */, &stacksize);
  if (stacklo == NULL) {
    perror("t_alloc_stack");
    return NULL;
  }

  // tspace is the memory at bottom of stack used for T
  size_t tspace = align2(sizeof(T), STACK_ALIGN);
  T* t = (T*)&stacklo[stacksize - tspace];
  // T* t = memalloct(NULL, T);
  // memset(t, 0, sizeof(T));
  t->status = TIdle;
  t->stacklo = stacklo;
  t->stackhi = &stacklo[stacksize];
  t->stackctx =
    make_fcontext(&stacklo[stacksize - tspace], stacksize - tspace, t_main);
  t->schedlink = NULL;

  #ifdef USE_MPROTECT
  dlog("allocated stack [top %zx ... %zx bottom] (%zuK usable + %zuK guard)",
    (uintptr_t)&t->stacklo[stack_guard_size],
    (uintptr_t)t->stackhi,
    (STACK_SIZE(t) - stack_guard_size) / 1024,
    stack_guard_size / 1024);
  #else
  dlog("allocated stack [top %zx ... %zx bottom] (%zuB)",
    (uintptr_t)t->stacklo,
    (uintptr_t)t->stackhi,
    (uintptr_t)t->stackhi - (uintptr_t)t->stacklo);
  #endif

  return t;
}

static T* spawn(void(*fn)(T*)) {
  T* t = t_new();
  if (t == NULL)
    return NULL;
  t->fn = fn;
  // jump_fcontext spends only 19 CPU cycles on x86_64 (compared to 1130 for ucontext)
  // See boost/context/doc/html/context/performance.html
  fctx_transfer_t tr = jump_fcontext(t->stackctx, t);
  return t;
}

static void yield(T* t) {
  t->status = TYielding;
  fctx_transfer_t tr = jump_fcontext(t->parentctx, t);
  t->status = TRunning;
}

static void fun1(T* t);

static void fun2(T* t) {
  dlog("fun2");
  dlog("fun2: calling yield");
  yield(t);
  dlog("fun2: returned from yield");
}

static void fun1(T* t) {
  u8 stackblob[128]; // take up a lot of stack space
  dlog("fun1 SB: %zx, local: %zx, dist: %zu B",
    (uintptr_t)t->stackhi,
    (uintptr_t)&stackblob[0],
    ((uintptr_t)t->stackhi - (uintptr_t)&stackblob[0]));

  // dlog("fun1: calling yield");
  // yield(t);
  // dlog("fun1: returned from yield");

  dlog("fun1: calling spawn fun2");
  auto t2 = spawn(fun2);
  dlog("fun1: spawn fun2 returned");

  //fun1(t); // mprotect will stop the process after the stack has been used up
}

void fctx_test() {
  dlog("fctx_test");
  //init_sighandler();
  // auto t1 = spawn(fun1);
  dlog("fctx_test: spawning new task with fun1");

  auto t1 = spawn(fun1);
  while (t1->status == TYielding) {
    dlog("fctx_test: task yielded; resume");
    jump_fcontext(t1->stackctx, t1);
  }

  dlog("fctx_test: spawn returned: %p", t1);
  // while (t1 && resume(t1) != TDead) {
  //   //
  // }
  // dlog("test1 end");
  exit(0);
}


// static jmp_buf* sigbus_jmp = NULL;

static void signal_handler(int sig) {
  dlog("signal_handler: sig %d", sig);
  // if (sig == SIGBUS) {
  //   if (sigbus_jmp)
  //     siglongjmp(*sigbus_jmp, 1);
  //   // no one to catch the error, so abort
  //   abort();
  // }
}

static void init_sighandler() {
  signal(SIGBUS, signal_handler);
}
