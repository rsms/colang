/*
 * Execution context switching
 * AMD64 Sys V ABI (GAS assemly flavor)
 *
 * exectx_call(uintptr arg, fn(uintptr arg), uintptr SP)
 *   Calls fn with arg on stack at SP
 *
 * exectx_setup(exectx_state_t, fn(uintptr arg), uintptr arg, uintptr SP)
 *   Sets up exectx_state_t data with entry point fn
 *
 * _exectx_resume(exectx_state_t, uintptr arg)
 *   Restores execution state and continues execution either at fn passed to exectx_setup
 *   or the most recent exectx_save call.
 *   _exectx_resume behaves like libc's longjmp()
 *
 * exectx_save(exectx_state_t) -> uintptr
 *   Saves current execution context.
 *   Returns multiple times: First time it returns 0 after it saved.
 *   Subsequent times it returns (resumes) returning the value passed to _exectx_resume.
 *   exectx_save behaves like libc's setjmp()
 *
 */

// Notes on AMD64 Sys V ABI:
//
// Callee-owned registers.
// Caller needs to save these before a call (if caller uses them.)
// Callee can freely use these registers.
//   RAX    Return value
//   RDI    1st argument
//   RSI    2nd argument
//   RDX    3rd argument
//   RCX    4th argument
//   R8     5th argument
//   R9     6th argument
//   R10    Scratch
//   R11    Scratch
//
// Caller-owned registers:
// Caller does not need to save these registers.
// Callee using these must save and later restore their values before returning.
//   RSP    Stack pointer
//   RBX    Local variable
//   RBP    Local variable
//   R12    Local variable
//   R13    Local variable
//   R14    Local variable
//   R15    Local variable
//
// Addressing modes:
//   movl $1, 0x604892          Direct (address is constant value)
//   movl $1, (%rax)            Indirect (address is in register %rax)
//   movl $1, -24(%rbp)         Indirect with displacement
//                                (addr = base %rbp + displ -24)
//   movl $1, 8(%rsp, %rdi, 4)  Indirect with displacement and scaled-index
//                                (addr = base %rsp + displ 8 + index %rdi * scale 4)
//   movl $1, (%rax, %rcx, 8)   Special case scaled-index, displ assumed 0
//   movl $1, 0x8(, %rdx, 4)    Special case scaled-index, base assumed 0
//   movl $1, 0x4(%rax, %rcx)   Special case scaled-index, scale assumed 1
//
// Integer arguments are passed (in order) in these regs:
//   %rdi, %rsi, %rdx, %rcx, %r8, %r9
//

// offsets into exectx_state_t where we save state
#define JB_RBX        0
#define JB_RBP        8
#define JB_RSP        16
#define JB_R12        24
#define JB_R13        32
#define JB_R14        40
#define JB_R15        48
#define JB_RIP        56
#define JB_RFLAGS     64
#define JB_MXCSR      72
#define JB_FPCONTROL  76

.text


// void exectx_call(uintptr_t arg, void(*fn)(uintptr_t arg), void* sp)
//
// exectx_call is functionally equivalent to the following two calls ...
//   exectx_setup(exectx_state_t tmp, fn, arg, sp)
//   _exectx_resume(exectx_state_t tmp, arg)
// ... but is implemented in much more efficient way.
//
.global _exectx_call
.align 8
_exectx_call:
    // arg 1 arg is in %rdi
    // arg 2 fn  is in %rsi
    // arg 3 sp is in  %rdx
    leaq  __abort(%rip), %rbp  // if fn returns, jump to __abort
    movq  %rdx, %rsp           // set stack pointer from arg 4
    cld                        // CLear Direction Flag (DF)
    push  %rbp                 // store return address on stack and fix stack alignment
    jmp   *%rsi                // jump into fn (RDI already contains arg)

__abort:
    call _abort  // fn passed to exectx_call should never return
    hlt



// void exectx_setup(exectx_state_t, void(*fn)(uintptr_t arg), uintptr_t arg, void* sp)
// Note: This function is intimately connected with exectx_resume
.globl _exectx_setup
.align 8
_exectx_setup:
    // arg 1 ctx is in %rdi
    // arg 2 fn  is in %rsi
    // arg 3 arg is in %rdx
    // arg 4 sp  is in %rcx

    movq  %rsi, JB_RBX(%rdi); // save arg 2: fn (jump target address) in RBX
    movq  %rdx, JB_R12(%rdi); // save arg 3: arg in R12
    movq  %rcx, JB_RSP(%rdi); // save arg 4: sp (stack pointer, stack high address) in RSP

    fnstcw  JB_FPCONTROL(%rdi); // save fp control word
    stmxcsr JB_MXCSR(%rdi);     // save MXCSR

    // compute abs address of finish procedure (entered after fn returns)
    leaq  exectx_setup_finish(%rip), %rcx;
    movq  %rcx, JB_RBP(%rdi);

    // compute abs address of entry trampoline
    leaq  exectx_setup_trampoline(%rip), %rcx;
    movq  %rcx, JB_RIP(%rdi);

    ret

exectx_setup_trampoline:
    push %rbp       // store return address on stack AND fix stack alignment
    movq %r12, %rdi // Copy arg1 for fn from R12 (loaded by exectx_resume)
    jmp *%rbx       // jump to destination at RBX (loaded by exectx_resume)

exectx_setup_finish:
    //xorq  %rdi, %rdi  // exit code is zero
    call  __t_exit0     // exit task
    hlt



// void _exectx_resume(exectx_state_t, uintptr_t)
// Note: This function is intimately connected with exectx_setup
.global __exectx_resume
.align 8
__exectx_resume:
    // %rdi is a exectx_state_t
    // %rsi is the return value
    //fninit     // Clear all FP exceptions

    // set arg 2 as return value for exectx_save
    movq    %rsi, %rax

    // general registers (RDI is the exectx_state_t, restore regs from it)
    movq    JB_RBX(%rdi), %rbx
    movq    JB_RBP(%rdi), %rsi
    movq    %rsi, %rbp
    movq    JB_RSP(%rdi), %rsi
    #ifdef DEBUG
    movsbq  (%rsi), %r12    // probe to detect absolutely corrupt stack pointers
    #endif
    movq    %rsi, %rsp
    movq    JB_R12(%rdi), %r12
    movq    JB_R13(%rdi), %r13
    movq    JB_R14(%rdi), %r14
    movq    JB_R15(%rdi), %r15
    movq    JB_RIP(%rdi), %rsi  // jump destination

    fldcw   JB_FPCONTROL(%rdi) // restore FP control word
    ldmxcsr JB_MXCSR(%rdi)    // restore MXCSR
    cld  // CLear Direction Flag (DF)
    jmp     *%rsi



// uintptr_t exectx_save(exectx_state_t)
.globl _exectx_save
.align 8
_exectx_save:
    // %rdi is a exectx_state_t
    movq    %rbx, JB_RBX(%rdi)

    // movq    %rbp, %rax
    // movq    %rax, JB_RBP(%rdi)
    movq    %rbp, JB_RBP(%rdi)

    // Save GP regs
    movq    %r12, JB_R12(%rdi)
    movq    %r13, JB_R13(%rdi)
    movq    %r14, JB_R14(%rdi)
    movq    %r15, JB_R15(%rdi)

    // RIP is set to the frame return address value
    movq    (%rsp), %rax
    movq    %rax, JB_RIP(%rdi)

    // RSP is set to the frame return address plus 8 (just after call to exectx_save)
    leaq    8(%rsp), %rax
    movq    %rax, JB_RSP(%rdi)

    fnstcw  JB_FPCONTROL(%rdi)  // save fp control word
    stmxcsr JB_MXCSR(%rdi)      // save MXCSR
    xorq    %rax, %rax          // set return value to 0
    ret
