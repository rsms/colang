/*
 * Execution context switching
 * ARM64/aarch64 AAPCS (AArch64 Procedure Call Standard)
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

// Notes on (ELF) AArch64 Procedure Call Standard (aka AAPCS64):
// https://github.com/ARM-software/abi-aa/releases
// https://github.com/ARM-software/abi-aa/blob/f52e1ad3f81254497a83578dc102f6/aapcs64/aapcs64.rst
//
// Callee-owned (caller-saved, temporary) registers.
// Caller needs to save these before a call (if caller uses them.)
// Callee can freely use these registers.
//   r0    1st argument or return value
//   r1    2nd argument or return value
//   r2    3rd argument or return value
//   r3    4th argument or return value
//   r4    5th argument or return value
//   r5    6th argument or return value
//   r6    7th argument or return value
//   r7    8th argument or return value
//   r8    General purpose (May be used as Indirect result location register)
//   r9    General purpose
//   ...   ...
//   r15   General purpose
//   r16   General purpose (May be used by some linkers as "IP0")
//   r17   General purpose (May be used by some linkers as "IP1")
//   r18   General purpose (May be used as the Platform register)
//
// Caller-owned (callee-saved, long-lived) registers:
// Caller does not need to save these registers.
// Callee using these must save and later restore their values before returning.
//   r19   General purpose
//   ...   ...
//   r28   General purpose
//
// Special registers:
//   SP    Stack pointer
//   r8    Indirect result location register
//   r18   The Platform register, if needed; otherwise a temporary register.
//         The role of register r18 is platform specific. If a platform ABI has
//         need of a dedicated general purpose register to carry inter-procedural
//         state (for example, the thread context) then it should use this
//         register for that purpose. If the platform ABI has no such
//         requirements, then it should use r18 as an additional temporary
//         register. The platform ABI specification must document the usage for
//         this register.
//   r29   Frame pointer (aka FP)
//   r30   Link register (aka LR)
//
//
// Addressing modes:
// See https://modexp.wordpress.com/2018/10/30/arm64-assembly/#loadstore
//   LDR (B|H|SB|SH|SW)   Wt, [Xn|SP], #simm
//     Load Register (immediate) loads a word or doubleword from memory and writes it
//     to a register. The address that is used for the load is calculated from a base
//     register and an immediate offset.
//     For example, LDRB with the B at the end means to load a byte.
//
//   LDR (B|H|SB|SH|SW)   Wt, [Xn|SP, (Wm|Xm){, extend {amount}}]
//     Load Register (register) calculates an address from a base register value and
//     an offset register value, loads a byte/half-word/word from memory, and writes it
//     to a register. The offset register value can optionally be shifted and extended.
//
//   STR (B|H|SB|SH|SW)   Wt, [Xn|SP], #simm
//   STR (B|H|SB|SH|SW)   Wt, [Xn|SP, (Wm|Xm){, extend {amount}}]
//     Store Register (immediate) stores a word or a doubleword from a register to memory.
//     The address that is used for the store is calculated from a base register and an
//     immediate offset.
//
//   LDP   Wt1, Wt2, [Xn|SP], #imm
//     Load Pair of Registers calculates an address from a base register value and an
//     immediate offset, loads two 32-bit words or two 64-bit doublewords from memory,
//     and writes them to two registers.
//
//   STP   Wt1, Wt2, [Xn|SP], #imm
//     Store Pair of Registers calculates an address from a base register value and an
//     immediate offset, and stores two 32-bit words or two 64-bit doublewords to the
//     calculated address, from two registers
//
// Integer arguments and return values are passed in these regs: (in order)
//   x0 ... x7
//

// offsets into exectx_state_t where we save state
#define JMP_r19_20  #0x00
#define JMP_r21_22  #0x10
#define JMP_r23_24  #0x20
#define JMP_r25_26  #0x30
#define JMP_r27_28  #0x40
#define JMP_r29_30  #0x50 /* FP & LR */
#define JMP_r30     #0x58 /* LR */
#define JMP_d8_d9   #0x60
#define JMP_d10_d11 #0x70
#define JMP_d12_d13 #0x80
#define JMP_d14_d15 #0x90
#define JMP_sp_pc   #0xA0

// helper macro for definint an exported function entry point
#define ENTRY_POINT(name) \
  .align 2               ;\
  .globl  name           ;\
  .type name,@function   ;\
  .text                  ;\
name:


// void exectx_call(uintptr_t arg, void(*fn)(uintptr_t arg), void* sp)
ENTRY_POINT(exectx_call)
    // arg arg : x0
    // arg fn  : x1
    // arg sp  : x2
    adr  x3,     exectx_call_end  // load address of label to enter after fn returns
    str  x3,     [x0, JMP_r30]    // store address of label at r30
    mov  sp, x2                   // set stack pointer
    br   x1                       // branch to PC (its arg is already in x0)

exectx_call_end:
    // abort's location is not known until link time, thus the label
    bl   abort
    wfi

// void exectx_setup(exectx_state_t, void(*fn)(uintptr_t arg), uintptr_t arg, void* sp)
// Note: This function is intimately connected with _exectx_resume
ENTRY_POINT(exectx_setup)
    // arg ctx : x0
    // arg fn  : x1
    // arg arg : x2
    // arg sp  : x3
    stp  x1, x2, [x0, JMP_r19_20]        // store fn,arg in slots r19,r20
    adr  x1,     exectx_setup_trampoline // load address of label
    stp  x3, x1, [x0, JMP_sp_pc]         // store sp and address of label
    adr  x1,     _t_exit0                // load address of label to enter after fn returns
    str  x1,     [x0, JMP_r30]           // store address of label at r30
    ret

exectx_setup_trampoline:
    // fn,arg are in x19,x20  (loaded by _exectx_resume)
    mov  x0, x20   // set 1st arg for fn to arg provided to exectx_setup
    br   x19       // jump to fn


// void _exectx_resume(exectx_state_t, uintptr_t)
// Note: This function is intimately connected with exectx_setup
ENTRY_POINT(_exectx_resume)
    // arg ctx : x0
    // arg arg : x1
    ldp  x19, x20,  [x0, JMP_r19_20]
    ldp  x21, x22,  [x0, JMP_r21_22]
    ldp  x23, x24,  [x0, JMP_r23_24]
    ldp  x25, x26,  [x0, JMP_r25_26]
    ldp  x27, x28,  [x0, JMP_r27_28]
    ldp  x29, x30,  [x0, JMP_r29_30]  // FP, RL/PC
    ldp  d8,  d9,   [x0, JMP_d8_d9]
    ldp  d10, d11,  [x0, JMP_d10_d11]
    ldp  d12, d13,  [x0, JMP_d12_d13]
    ldp  d14, d15,  [x0, JMP_d14_d15]
    ldp  x2,  x3,   [x0, JMP_sp_pc]   // load stack pointer and PC
    mov  sp,  x2                      // set stack pointer
    br   x3                           // branch to PC


// uintptr_t exectx_save(exectx_state_t)
ENTRY_POINT(exectx_save)
    // arg ctx : x0
    stp  x19, x20,  [x0, JMP_r19_20]
    stp  x21, x22,  [x0, JMP_r21_22]
    stp  x23, x24,  [x0, JMP_r23_24]
    stp  x25, x26,  [x0, JMP_r25_26]
    stp  x27, x28,  [x0, JMP_r27_28]
    stp  x29, x30,  [x0, JMP_r29_30]
    stp  d8,  d9,   [x0, JMP_d8_d9]
    stp  d10, d11,  [x0, JMP_d10_d11]
    stp  d12, d13,  [x0, JMP_d12_d13]
    stp  d14, d15,  [x0, JMP_d14_d15]
    mov  x1,  sp                      // read stack pointer (can't use directly in STP)
    stp  x1,  x30,  [x0, JMP_sp_pc]   // store stack pointer and PC
    mov  x0,  #0                      // set return value to 0
    ret
