#pragma once

typedef struct exectx_t {
  void* ctx;
  void* data;
} exectx_t;

// // max number of args
// #define EXECTX_MAX_ARGS

// exectx_state_t
#if defined(__x86_64__)
// rflags, rip, rbp, rsp, rbx, r12, r13, r14, r15... these are 8 bytes each
// mxcsr, fp control word, sigmask... these are 4 bytes each
// (Note: musl reserves 200 B)
typedef int exectx_state_t[(9 * 2) + 3]; // 84 B

#elif defined(__i386__)
// eax, ebx, ecx, edx, edi, esi, ebp, esp, ss, eflags, eip,
// cs, de, es, fs, gs == 16 ints
// onstack, mask = 2 ints
// (Note: musl reserves 156 B)
typedef int exectx_state_t[16 + 2]; // 72 B

#elif defined(__arm__) && !defined(__ARM_ARCH_7K__)
// r4-r8, r10, fp, sp, lr, sig  == 10 register_t sized
// s16-s31 == 16 register_t sized + 1 int for FSTMX
// 1 extra int for future use
// (Note: musl reserves 292 B)
typedef int exectx_state_t[10 + 16 + 2]; // 112 B

#elif defined(__arm64__) || defined(__aarch64__) || defined(__ARM_ARCH_7K__)
// r21-r29, sp, fp, lr == 12 registers, 8 bytes each. d8-d15
// are another 8 registers, each 8 bytes long. (aapcs64 specifies
// that only 64-bit versions of FP registers need to be saved).
// Finally, two 8-byte fields for signal handling purposes.
// (Note: musl reserves 312 B)
typedef int exectx_state_t[(14 + 8 + 2) * 2]; // 192 B

#else
#error "unsupported arch"
#endif

// Functionality overview:
//
// A. save M execution state
//    - set "resume point" from a specific execution state (i.e. setjmp)
//
// B. init coroutine execution state
//    - set entry function as the next "resume point"
//
// C. resume coroutine execution
//    - restore coroutine execution state from T.stack
//    - longjmp to wherever it was last executing
//    - (called by t_execute; no need to save state in M.t0.stack)
//
// D. from a coroutine, execute a function on M's stack
//    - save coroutine execution state to T.stack
//    - restore M's execution state from M.t0.stack
//    - longjmp to fn
//
// Example timeline: (step, function, execution context, action)
//    1     [M]  start M
//    2  A  [M]  save M execution state
//    3  B  [M]  init coroutine T execution state
//    4  C  [M]  resume coroutine T execution (t_execute)
//    5     [T]  coroutine entry function called
//    6  D  [T]  execute a function F on M's stack (m_call)
//    7     [M]  m_call function F called
//    8  C  [M]  resume coroutine T execution (t_execute)
//    9     [T]  coroutine resumes exection (after step 6)
//   10  D  [T]  exits; execute a function F on M's stack (m_call)
//   11     [M]  cleans up after T
//   12  C  [M]  resume some other coroutine T2 execution (t_execute)
//       ...
//
// Notes:
//   - B and D are similar
//   The way go works when a coroutine T1 calls "go foo" is that...
//     1. save T1's return address after the "go" call as callerpc
//     2. create new T2 and set T2.gopc = callerpc (used only for tracebacks) and
//        initialize T2's stack to enter at "foo" (gostartcallfn)
//     3. add T2 to M's runqueue
//     4. wake up a new P to execture T2 (may do nothing)
//     5. return from "go" call in T1 (resumes T1 on current P & M)
//


void exectx_setup(exectx_state_t s, void(*fn)(uintptr_t arg), uintptr_t arg, void* sp);

// exectx_call calls fn with arg on stack sp
void noreturn exectx_call(uintptr_t arg, void(*fn)(uintptr_t arg), void* sp);


// exectx_save saves the callers execution context into s and returns twice:
// first time, after saving, it returns 0 and the second time it returns from a call
// to exectx_resume with the value provided to exectx_resume.
//
// Example:
//   exectx_state_t s;
//   if (exectx_save(s) == 0) {
//     puts("saved state");
//     exectx_resume(s, 1);
//     puts("never reached");
//   } else {
//     puts("restored");
//   }
// Output:
//   saved state
//   restored
//
uintptr_t exectx_save(exectx_state_t s);

// exectx_resume restores the execution context at s (i.e. registers), causing execution
// to move to a previous call to exectx_save on the same s, returning saveret to the
// corresponding exectx_save call.
// You must never pass 0 for saveret as that might cause an infinite loop at exectx_save.
void _exectx_resume(exectx_state_t s, uintptr_t saveret);
#define exectx_resume(s,r) ({ assert(r != 0); _exectx_resume((s),(r)); })


// exectx_init initializes memory ending at sp (stack base pointer) by setting up a call to fn.
// The size argument should be the total size of memory below sp. This function will only use
// a small portion of the memory below sp (64 bytes on x86_64.)
// Calling exectx_switch or exectx_jump with the returned handle will execute fn.
void* exectx_init(void* sp, size_t size, void(*fn)(exectx_t));




// exectx_t ontop_fcontext(void* const to, void* data, exectx_t(*fn)(exectx_t) );

// exectx_switch saves the current (caller) execution context on the caller stack, then
// restores registers from ctx and finally jumps to wherever ctx left off (or fn from exectx_init
// if switch has not been called before for this ctx.)
// Effectively it both does exectx_save and exectx_jump
exectx_t exectx_switch(void* const ctx, void* data);

// exectx_jump restores exection at ctx, replacing the current exection context
exectx_t noreturn exectx_jump(void* const ctx, void* data);

// exectx_callerpc returns the program counter (PC) of its caller's caller.
// exectx_callersp returns the stack pointer (SP) of its caller's caller.
// The implementation may be a compiler intrinsic; there is not
// necessarily code implementing this on every platform.
//
// For example:
//
//  static void f(int arg1, int arg2, int arg3) {
//    uintptr_t pc = exectx_callerpc();
//    uintptr_t sp = exectx_callersp();
//  }
//
// These two lines find the PC and SP immediately following
// the call to f (where f will return).
//
// The call to exectx_callerpc and exectx_callersp must be done in the
// frame being asked about.
//
// The result of exectx_callersp is correct at the time of the return,
// but it may be invalidated by any subsequent call to a function
// that might relocate the stack in order to grow or shrink it.
// A general rule is that the result of exectx_callersp should be used
// immediately and can only be passed to nosplit functions.
//
//void* exectx_callerpc();
//uintptr_t exectx_callersp();

// void * __builtin_return_address (unsigned int level)

#define exectx_callerpc() \
  (uintptr_t)(__builtin_return_address(0))

#define exectx_callersp() \
  (uintptr_t)(0) /* TODO */

// #define dill_setsp(x) \
//     asm(""::"r"(alloca(sizeof(size_t))));\
//     asm volatile("leaq (%%rax), %%rsp"::"rax"(x));
