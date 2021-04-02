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
