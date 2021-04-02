/*
            Copyright Oliver Kowalke 2009.
   Distributed under the Boost Software License, Version 1.0.
      (See accompanying file LICENSE_1_0.txt or copy at
            http://www.boost.org/LICENSE_1_0.txt)
*/

/****************************************************************************************
 *                                                                                      *
 *  ----------------------------------------------------------------------------------  *
 *  |    0    |    1    |    2    |    3    |    4     |    5    |    6    |    7    |  *
 *  ----------------------------------------------------------------------------------  *
 *  |   0x0   |   0x4   |   0x8   |   0xc   |   0x10   |   0x14  |   0x18  |   0x1c  |  *
 *  ----------------------------------------------------------------------------------  *
 *  | fc_mxcsr|fc_x87_cw|        R12        |         R13        |        R14        |  *
 *  ----------------------------------------------------------------------------------  *
 *  ----------------------------------------------------------------------------------  *
 *  |    8    |    9    |   10    |   11    |    12    |    13   |    14   |    15   |  *
 *  ----------------------------------------------------------------------------------  *
 *  |   0x20  |   0x24  |   0x28  |  0x2c   |   0x30   |   0x34  |   0x38  |   0x3c  |  *
 *  ----------------------------------------------------------------------------------  *
 *  |        R15        |        RBX        |         RBP        |        RIP        |  *
 *  ----------------------------------------------------------------------------------  *
 *                                                                                      *
 ****************************************************************************************/

.text

// void* exectx_init(void* sp, size_t size, void(*fn)(exectx_t))
// exectx_init initializes memory ending at sp (stack base pointer)
.globl _exectx_init
.align 8
_exectx_init:
    /* first arg of exectx_init() == top of context-stack */
    movq  %rdi, %rax

    /* shift address in RAX to lower 16 byte boundary */
    andq  $-16, %rax

    /* reserve space for context-data on context-stack */
    /* on context-function entry: (RSP -0x8) % 16 == 0 */
    leaq  -0x40(%rax), %rax

    /* third arg of exectx_init() == address of context-function */
    /* stored in RBX */
    movq  %rdx, 0x28(%rax)

    /* save MMX control- and status-word */
    stmxcsr  (%rax)
    /* save x87 control-word */
    fnstcw   0x4(%rax)

    /* compute abs address of label trampoline */
    leaq  trampoline(%rip), %rcx
    /* save address of trampoline as return-address for context-function */
    /* will be entered after calling jump_fcontext() first time */
    movq  %rcx, 0x38(%rax)

    /* compute abs address of label finish */
    leaq  finish(%rip), %rcx
    /* save address of finish as return-address for context-function */
    /* will be entered after context-function returns */
    movq  %rcx, 0x30(%rax)

    ret /* return pointer to context-data */

trampoline:
    /* store return address on stack */
    /* fix stack alignment */
    push %rbp
    /* jump to context-function */
    jmp *%rbx

finish:
    /* exit code is zero */
    xorq  %rdi, %rdi
    /* exit application */
    call  __exit
    hlt


// // uintptr_t exectx_save(exectx_state_t)
// // This code comes from musl libc's setjmp
// .globl _exectx_save
// .align 8
// _exectx_save:
//     mov %rbx,(%rdi)         /* rdi is exectx_state_t, move registers onto it */
//     mov %rbp,8(%rdi)
//     mov %r12,16(%rdi)
//     mov %r13,24(%rdi)
//     mov %r14,32(%rdi)
//     mov %r15,40(%rdi)
//     lea 8(%rsp),%rdx        /* this is our rsp WITHOUT current ret addr */
//     mov %rdx,48(%rdi)
//     mov (%rsp),%rdx         /* save return addr ptr for new rip */
//     mov %rdx,56(%rdi)
//     xor %eax,%eax           /* always return 0 */
//     ret

// // void exectx_resume(exectx_state_t, uintptr_t)
// // This code comes from musl libc's longjmp
// .global _exectx_resume
// .align 8
// _exectx_resume:
//     xor %eax,%eax
//     cmp $1,%esi             /* CF = val ? 0 : 1 */
//     adc %esi,%eax           /* eax = val + !val */
//     mov (%rdi),%rbx         /* rdi is the exectx_state_t, restore regs from it */
//     mov 8(%rdi),%rbp
//     mov 16(%rdi),%r12
//     mov 24(%rdi),%r13
//     mov 32(%rdi),%r14
//     mov 40(%rdi),%r15
//     mov 48(%rdi),%rsp
//     jmp *56(%rdi)           /* goto saved address without altering rsp */



#define JB_RBX          0
#define JB_RBP          8
#define JB_RSP          16
#define JB_R12          24
#define JB_R13          32
#define JB_R14          40
#define JB_R15          48
#define JB_RIP          56
#define JB_RFLAGS       64
#define JB_MXCSR        72
#define JB_FPCONTROL    76

// Note AMD64 Sys V ABI uses the following regs in order for integer arguments:
// %rdi, %rsi, %rdx, %rcx, %r8, %r9

// void exectx_setup(exectx_state_t ctx, void* sp, void(*fn)(uintptr_t arg))
.globl _exectx_setup
.align 8
_exectx_setup:
    // arg 1 ctx is in %rdi
    // arg 2 sp  is in %rsi
    // arg 3 fn  is in %rdx

    // // RSP is set to the frame return address plus 8 + sizeof(va_list)=24
    // leaq    32(%rsp), %rax
    // movq    %rax, JB_RSP(%rdi)

    // set jump-to address for future exectx_resume call
    // movq  %rdx, JB_RIP(%rdi)

    // // stack base
    // movq  %rsi, %rax
    // // sub   $4096, %rax
    // add   $4096, %rax
    // movq  %rax, JB_RBP(%rdi)

    // /* shift address in RAX to lower 16 byte boundary */
    // andq  $-16, %rdi

    // save jump target address in RBX
    movq  %rdx, JB_RBX(%rdi)

    // arg 2 is stack pointer (top of stack; high address)
    // andq  $-16, %rsi  // shift address to lower 16 byte boundary
    //                   // (unused; we make sure to align sp before calling.)
    movq  %rsi, JB_RSP(%rdi)

    fnstcw  JB_FPCONTROL(%rdi) // save fp control word
    stmxcsr JB_MXCSR(%rdi)     // save MXCSR

    // compute abs address of label trampoline
    leaq  exectx_setup_trampoline(%rip), %rcx
    /* save address of trampoline as return-address for context-function */
    /* will be entered after calling jump_fcontext() first time */
    movq  %rcx, JB_RIP(%rdi)

    /* compute abs address of label finish */
    leaq  exectx_setup_finish(%rip), %rcx
    /* save address of finish as return-address for context-function */
    /* will be entered after context-function returns */
    movq  %rcx, JB_RBP(%rdi)

    // return
    // xorq    %rax, %rax // return 0
    ret

exectx_setup_trampoline:
    push %rbp // store return address on stack AND fix stack alignment
    // sub $8, %rsp // fix stack alignment

    // Copy return value intended for exectx_save to argument 1 for fn.
    // This is the value (second argument) passed to exectx_resume.
    movq %rax, %rdi

    jmp *%rbx // exectx_resume has loaded destination into rbx

exectx_setup_finish:
    //xorq  %rdi, %rdi  // exit code is zero
    call  __t_exit0     // exit task
    hlt

// uintptr_t exectx_save(exectx_state_t)
.globl _exectx_save
.align 8
_exectx_save:
    // %rdi is a exectx_state_t
    movq    %rbx, JB_RBX(%rdi)
    movq    %rbp, %rax
    movq    %rax, JB_RBP(%rdi)
    movq    %r12, JB_R12(%rdi)
    movq    %r13, JB_R13(%rdi)
    movq    %r14, JB_R14(%rdi)
    movq    %r15, JB_R15(%rdi)

    // RIP is set to the frame return address value
    movq    (%rsp), %rax
    movq    %rax, JB_RIP(%rdi)
    // RSP is set to the frame return address plus 8
    leaq    8(%rsp), %rax
    movq    %rax, JB_RSP(%rdi)

    // save fp control word
    fnstcw  JB_FPCONTROL(%rdi)

    // save MXCSR
    stmxcsr JB_MXCSR(%rdi)

    // return 0
    xorq    %rax, %rax
    ret


// void _exectx_resume(exectx_state_t, uintptr_t)
.global __exectx_resume
.align 8
__exectx_resume:
    // %rdi is a exectx_state_t
    // %rsi is the return value
    fninit     // Clear all FP exceptions

    // argument 2
    movq    %rsi, %rax
    // Here's saved code from longjmp which ensures argument is not 0
    //   testq   %rsi, %rsi
    //   movq    $1, %rax
    //   cmovneq %rsi, %rax
    // Another way of doing the above:
    //   xor %rax,%rax
    //   cmp $1,%rsi       /* CF = val ? 0 : 1 */
    //   adc %rsi,%rax     /* eax = val + !val */

    // general registers
    movq    JB_RBX(%rdi), %rbx // rdi is the exectx_state_t, restore regs from it
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
    // movq    80(%rdi), %r8

    fldcw   JB_FPCONTROL(%rdi) // restore FP control word
    ldmxcsr JB_MXCSR(%rdi)    // restore MXCSR
    cld  // CLear Direction Flag (DF)
    jmp     *%rsi



// void _exectx_call(exectx_state_t, void(*fn)(void* arg), void* arg)
// calls fn on stack exectx_state_t
.global _exectx_call
.align 8
_exectx_call:
    // arg 1 exectx_state_t is in %rdi
    // arg 2 fn             is in %rsi
    // arg 3 data           is in %rdx
    movq  %rsi, %r8  // arg 2: preserve function in R8
    //movq  %rdx, %r9  // arg 3: preserve user argument in R9
    //
    // Note AMD64 Sys V ABI uses the following regs in order for integer arguments:
    // %rdi, %rsi, %rdx, %rcx, %r8, %r9

    // %rdi is a exectx_state_t
    // %rsi is the return value
    fninit     // Clear all FP exceptions

    // restore general registers
    movq    JB_RBX(%rdi), %rbx // rdi is the exectx_state_t, restore regs from it
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
    movq    JB_RIP(%rdi), %rsi

    fldcw   JB_FPCONTROL(%rdi) // restore FP control word
    ldmxcsr JB_MXCSR(%rdi)     // restore MXCSR
    cld                        // CLear Direction Flag (DF)

    // jmp *%rsi

    // pass data as arg 1 to "fn" function
    movq  %rdx, %rdi

    // keep return-address on stack
    jmp   *%r8  /* indirect jump to arg 2 "fn" */



// ontop:
// // exectx_t exectx_switchfn( void* toctx, void* arg, void(*fn)(void* arg) )
// .globl _exectx_switchfn
// .align 8
// _exectx_switchfn:
//     /* preserve ontop-function in R8 */
//     movq  %rdx, %r8

//     leaq  -0x38(%rsp), %rsp /* prepare stack */

//     stmxcsr  (%rsp)     /* save MMX control- and status-word */
//     fnstcw   0x4(%rsp)  /* save x87 control-word */

//     movq  %r12, 0x8(%rsp)  /* save R12 */
//     movq  %r13, 0x10(%rsp)  /* save R13 */
//     movq  %r14, 0x18(%rsp)  /* save R14 */
//     movq  %r15, 0x20(%rsp)  /* save R15 */
//     movq  %rbx, 0x28(%rsp)  /* save RBX */
//     movq  %rbp, 0x30(%rsp)  /* save RBP */

//     /* store RSP (pointing to context-data) in RAX */
//     movq  %rsp, %rax

//     /* restore RSP (pointing to context-data) from RDI */
//     movq  %rdi, %rsp

//     ldmxcsr  (%rsp)     /* restore MMX control- and status-word */
//     fldcw    0x4(%rsp)  /* restore x87 control-word */

//     movq  0x8(%rsp), %r12  /* restore R12 */
//     movq  0x10(%rsp), %r13  /* restore R13 */
//     movq  0x18(%rsp), %r14  /* restore R14 */
//     movq  0x20(%rsp), %r15  /* restore R15 */
//     movq  0x28(%rsp), %rbx  /* restore RBX */
//     movq  0x30(%rsp), %rbp  /* restore RBP */

//     leaq  0x38(%rsp), %rsp /* prepare stack */

//     /* return transfer_t from jump */
//     /* RAX == fctx, RDX == data */
//     movq  %rsi, %rdx
//     /* pass transfer_t as first arg in context function */
//     /* RDI == fctx, RSI == data */
//     movq  %rax, %rdi
//     /* keep return-address on stack */
//     jmp  *%r8  /* indirect jump */



// .globl _exectx_save
// .align 8
// _exectx_save:
//     leaq  -0x38(%rsp), %rsp /* prepare stack */

// #if !defined(BOOST_USE_TSX)
//     stmxcsr  (%rsp)     /* save MMX control- and status-word */
//     fnstcw   0x4(%rsp)  /* save x87 control-word */
// #endif

//     movq  %r12, 0x8(%rsp)  /* save R12 */
//     movq  %r13, 0x10(%rsp)  /* save R13 */
//     movq  %r14, 0x18(%rsp)  /* save R14 */
//     movq  %r15, 0x20(%rsp)  /* save R15 */
//     movq  %rbx, 0x28(%rsp)  /* save RBX */
//     movq  %rbp, 0x30(%rsp)  /* save RBP */

//     /* store RSP (pointing to context-data) in RAX */
//     movq  %rsp, %rax

//     ret



/* %%rsp */

// .text
// .globl _exectx_callerpc
// .align 8
// _exectx_callerpc:
//     movq (%rsp), %rax
//     ret


// .text
// .globl _exectx_callersp
// .align 8
// _exectx_callerpc:
//     movq %rsp, %rax
//     ret

