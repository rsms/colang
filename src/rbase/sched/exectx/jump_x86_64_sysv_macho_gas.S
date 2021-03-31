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

// [rsms] TODO: consider XMM registers. See:
// https://github.com/rust-lang/rust/blob/3d0d144283959bf62368f6965aae14ce95ee286b\
// /src/rt/arch/x86_64/_context.S

// _exectx_jump(void* to_ctx [%rdi], void* data [%rsi]) -> {from_ctx, from_data} [%rax]
//
// 1. allocate 38 bytes on local stack
// 2. save non-volatile register values on local stack
// 3. restore non-volatile register values from to_ctx [%rdi]
// 4. load user data [%rsi] into %rdx
// 5. jump to restored return address

.text
.globl _exectx_jump
.align 8
_exectx_jump:
    /* restore RSP (pointing to context-data) from RDI */
    movq  %rdi, %rsp

    movq  0x38(%rsp), %r8  /* restore return-address */

#if !defined(BOOST_USE_TSX)
    ldmxcsr  (%rsp)     /* restore MMX control- and status-word */
    fldcw    0x4(%rsp)  /* restore x87 control-word */
#endif

    movq  0x8(%rsp), %r12  /* restore R12 */
    movq  0x10(%rsp), %r13  /* restore R13 */
    movq  0x18(%rsp), %r14  /* restore R14 */
    movq  0x20(%rsp), %r15  /* restore R15 */
    movq  0x28(%rsp), %rbx  /* restore RBX */
    movq  0x30(%rsp), %rbp  /* restore RBP */

    leaq  0x40(%rsp), %rsp /* prepare stack */

    /* return transfer_t from jump */
    /* RAX == fctx, RDX == data */
    //movq  %rsi, %rdx
    /* pass transfer_t as first arg in context function */
    /* RDI == fctx, RSI == data */
    movq  %rax, %rdi

    /* indirect jump to context */
    jmp  *%r8
