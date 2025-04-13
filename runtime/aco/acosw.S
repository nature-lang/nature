// gcc 支持 .S 文件预处理

.text
.globl acosw
#if defined(__DARWIN)
#else
.type  acosw, @function
#endif
#ifndef __ARM64
.intel_syntax noprefix
#endif
acosw:
#ifdef __AMD64
/*
    0x00                  -->                  0xff
    r12 r13 r14 r15 rip rsp rbx rbp fpucw16 mxcsr32
    0   8   10  18  20  28  30  38  40      44
*/
    // rdi - from_co | rsi - to_co
    mov     rdx,QWORD PTR [rsp]      // retaddr
    lea     rcx,[rsp+0x8]            // rsp
    mov     QWORD PTR [rdi+0x0], r12
    mov     QWORD PTR [rdi+0x8], r13
    mov     QWORD PTR [rdi+0x10],r14
    mov     QWORD PTR [rdi+0x18],r15
    mov     QWORD PTR [rdi+0x20],rdx // retaddr
    mov     QWORD PTR [rdi+0x28],rcx // rsp
    mov     QWORD PTR [rdi+0x30],rbx
    mov     QWORD PTR [rdi+0x38],rbp
#ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
    fnstcw  WORD PTR  [rdi+0x40]
    stmxcsr DWORD PTR [rdi+0x44]
#endif
    mov     r12,QWORD PTR [rsi+0x0]
    mov     r13,QWORD PTR [rsi+0x8]
    mov     r14,QWORD PTR [rsi+0x10]
    mov     r15,QWORD PTR [rsi+0x18]
    mov     rax,QWORD PTR [rsi+0x20] // retaddr
    mov     rcx,QWORD PTR [rsi+0x28] // rsp
    mov     rbx,QWORD PTR [rsi+0x30]
    mov     rbp,QWORD PTR [rsi+0x38]
#ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
    fldcw   WORD PTR      [rsi+0x40]
    ldmxcsr DWORD PTR     [rsi+0x44]
#endif
    mov     rsp,rcx
    jmp     rax

#elif defined(__ARM64)
 /*
     0x00         -->            0xff
     x16 x17 x19  ...  x29 lr sp fpcr
     0   8   10   ...  60  68 70 78
 */
     // r0 - from_co | r1 - to_co
     mov     x2,  lr
     stp    x16, x17, [x0, 0x00]
     stp    x19, x20, [x0, 0x10]
     stp    x21, x22, [x0, 0x20]
     stp    x23, x24, [x0, 0x30]
     stp    x25, x26, [x0, 0x40]
     stp    x27, x28, [x0, 0x50]
     stp    x29, lr,  [x0, 0x60]
     mov     x5,  sp
     str     x5,  [x0, 0x70]
 #ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
     mrs     x5,  fpcr
     str     x5,  [x1, 0x78]
 #endif

     ldp     x16, x17, [x1, 0x00]
     ldp     x19, x20, [x1, 0x10]
     ldp     x21, x22, [x1, 0x20]
     ldp     x23, x24, [x1, 0x30]
     ldp     x25, x26, [x1, 0x40]
     ldp     x27, x28, [x1, 0x50]
     ldp     x29, x30, [x1, 0x60]
     ldr     x3,  [x1, 0x70]
     mov     sp,  x3
 #ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
     ldr     x3,  [x1, 0x78]
     msr     fpcr,x3
 #endif

     br      x30
#else
    #error "platform not support"
#endif


// ----------------------------------------------------------------------------------------
#ifdef __DARWIN
#define SYM(x) _##x
#else
#define SYM(x) x
#endif

#ifdef __DARWIN
.globl _async_preempt
_async_preempt:
#else
.globl async_preempt
.type  async_preempt, @function
async_preempt:
#endif
#ifdef __AMD64
    push rbp
    mov rbp, rsp
    pushfq
    sub rsp, 368
    nop
    mov QWORD PTR [rsp], rax
    mov QWORD PTR [rsp + 8], rcx
    mov QWORD PTR [rsp + 16], rdx
    mov QWORD PTR [rsp + 24], rbx
    mov QWORD PTR [rsp + 32], rsi
    mov QWORD PTR [rsp + 40], rdi
    mov QWORD PTR [rsp + 48], r8
    mov QWORD PTR [rsp + 56], r9
    mov QWORD PTR [rsp + 64], r10
    mov QWORD PTR [rsp + 72], r11
    mov QWORD PTR [rsp + 80], r12
    mov QWORD PTR [rsp + 88], r13
    mov QWORD PTR [rsp + 96], r14
    mov QWORD PTR [rsp + 104], r15

    movups [rsp + 112], xmm0
    movups [rsp + 128], xmm1
    movups [rsp + 144], xmm2
    movups [rsp + 160], xmm3
    movups [rsp + 176], xmm4
    movups [rsp + 192], xmm5
    movups [rsp + 208], xmm6
    movups [rsp + 224], xmm7
    movups [rsp + 240], xmm8
    movups [rsp + 256], xmm9
    movups [rsp + 272], xmm10
    movups [rsp + 288], xmm11
    movups [rsp + 304], xmm12
    movups [rsp + 320], xmm13
    movups [rsp + 336], xmm14
    movups [rsp + 352], xmm15
    call SYM(co_preempt_yield) // save and yield


    // debug (多的不能确定，但是这里肯定是 0 了，毕竟ret 地址被覆盖了，可关键是，栈空间还是会被污染呢)
    mov rdi, rbp
    mov rsi, [rbp + 8] // +8 是跳过 prev rbp, 此时栈中存储的值就是 ret addr 的值
    call SYM(debug_ret)

    movups xmm15, [rsp + 352]
    movups xmm14, [rsp + 336]
    movups xmm13, [rsp + 320]
    movups xmm12, [rsp + 304]
    movups xmm11, [rsp + 288]
    movups xmm10, [rsp + 272]
    movups xmm9, [rsp + 256]
    movups xmm8, [rsp + 240]
    movups xmm7, [rsp + 224]
    movups xmm6, [rsp + 208]
    movups xmm5, [rsp + 192]
    movups xmm4, [rsp + 176]
    movups xmm3, [rsp + 160]
    movups xmm2, [rsp + 144]
    movups xmm1, [rsp + 128]
    movups xmm0, [rsp + 112]
    mov r15, [rsp + 104]
    mov r14, [rsp + 96]
    mov r13, [rsp + 88]
    mov r12, [rsp + 80]
    mov r11, [rsp + 72]
    mov r10, [rsp + 64]
    mov r9, [rsp + 56]
    mov r8, [rsp + 48]
    mov rdi, [rsp + 40]
    mov rsi, [rsp + 32]
    mov rbx, [rsp + 24]
    mov rdx, [rsp + 16]
    mov rcx, [rsp + 8]
    mov rax, [rsp]
    add rsp, 368
    popfq

    pop rbp

    // thread_handle_sig 部分预留了一些栈空间，现在需要还原成与被抢占的函数一致
    // 但是此时栈空间还没有被污染，依旧可以从原空间中找到需要返回的 rip
    add rsp, 1032
    jmp [rsp - 1032]

#elif defined(__ARM64)
    // 保存所有通用寄存器
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #(32 * 8)      // 为通用寄存器分配空间

    // 保存通用寄存器
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]

    // 保存 NEON/FP 寄存器
    sub sp, sp, #(32 * 16)
    stp q0, q1, [sp, #0]
    stp q2, q3, [sp, #32]
    stp q4, q5, [sp, #64]
    stp q6, q7, [sp, #96]
    stp q8, q9, [sp, #128]
    stp q10, q11, [sp, #160]
    stp q12, q13, [sp, #192]
    stp q14, q15, [sp, #224]
    stp q16, q17, [sp, #256]
    stp q18, q19, [sp, #288]
    stp q20, q21, [sp, #320]
    stp q22, q23, [sp, #352]
    stp q24, q25, [sp, #384]
    stp q26, q27, [sp, #416]
    stp q28, q29, [sp, #448]
    stp q30, q31, [sp, #480]

    bl SYM(co_preempt_yield)

    // 恢复 NEON/FP 寄存器
    ldp q30, q31, [sp, #480]
    ldp q28, q29, [sp, #448]
    ldp q26, q27, [sp, #416]
    ldp q24, q25, [sp, #384]
    ldp q22, q23, [sp, #352]
    ldp q20, q21, [sp, #320]
    ldp q18, q19, [sp, #288]
    ldp q16, q17, [sp, #256]
    ldp q14, q15, [sp, #224]
    ldp q12, q13, [sp, #192]
    ldp q10, q11, [sp, #160]
    ldp q8, q9, [sp, #128]
    ldp q6, q7, [sp, #96]
    ldp q4, q5, [sp, #64]
    ldp q2, q3, [sp, #32]
    ldp q0, q1, [sp, #0]
    add sp, sp, #(32 * 16)

    // 恢复通用寄存器
    ldp x28, x29, [sp, #224]
    ldp x26, x27, [sp, #208]
    ldp x24, x25, [sp, #192]
    ldp x22, x23, [sp, #176]
    ldp x20, x21, [sp, #160]
    ldp x18, x19, [sp, #144]
    ldp x16, x17, [sp, #128]
    ldp x14, x15, [sp, #112]
    ldp x12, x13, [sp, #96]
    ldp x10, x11, [sp, #80]
    ldp x8, x9, [sp, #64]
    ldp x6, x7, [sp, #48]
    ldp x4, x5, [sp, #32]
    ldp x2, x3, [sp, #16]
    ldp x0, x1, [sp, #0]

    mov sp, x29
    ldp x29, x30, [sp], #16
    add sp, sp, #1032          // 恢复栈指针
    ret                        // 返回到被中断的位置

#else
#error "platform not support"
#endif




.globl aco_save_reg
#if defined(__DARWIN)
#else
.type  aco_save_reg, @function
#endif

#ifndef __ARM64
.intel_syntax noprefix
#endif

aco_save_reg:

#ifdef __AMD64
    mov     rdx,QWORD PTR [rsp]      // retaddr
    lea     rcx,[rsp+0x8]            // rsp
    mov     QWORD PTR [rdi+0x0], r12
    mov     QWORD PTR [rdi+0x8], r13
    mov     QWORD PTR [rdi+0x10],r14
    mov     QWORD PTR [rdi+0x18],r15
    mov     QWORD PTR [rdi+0x20],rdx // retaddr
    mov     QWORD PTR [rdi+0x28],rcx // rsp
    mov     QWORD PTR [rdi+0x30],rbx
    mov     QWORD PTR [rdi+0x38],rbp
#ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
    fnstcw  WORD PTR  [rdi+0x40]
    stmxcsr DWORD PTR [rdi+0x44]
#endif
    ret

#elif defined(__ARM64)
    // x0 包含目标地址
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x2, sp
    str x2, [x0, #96]
    ret

#else
    #error "platform not support"
#endif

.globl aco_save_fpucw_mxcsr
#if defined(__DARWIN)
#else
.type  aco_save_fpucw_mxcsr, @function
#endif
#ifndef __ARM64
.intel_syntax noprefix
#endif

aco_save_fpucw_mxcsr:
#ifdef __AMD64
    fnstcw  WORD PTR  [rdi]
    stmxcsr DWORD PTR [rdi+0x4]
    ret
#elif __ARM64
    mrs x1, fpcr
    str x1, [x0]
    ret
#else
    #error "platform not support"
#endif

#if defined(__DARWIN)
.globl _abort
.globl _aco_funcp_protector
#else
.globl abort
.globl aco_funcp_protector
#endif

.globl aco_funcp_protector_asm
#if defined(__DARWIN)
#else
.type  aco_funcp_protector_asm, @function
#endif

#ifndef __ARM64
.intel_syntax noprefix
#endif

aco_funcp_protector_asm:
#if defined(__AMD64)
    and     rsp,0xfffffffffffffff0
    call    SYM(aco_funcp_protector)
    call    SYM(abort)
    ret
#elif defined(__ARM64)
    mov     x9, sp
    and     x9, x9, #0xfffffffffffffff0
    mov     sp, x9
    #if defined(__DARWIN)
    bl      _aco_funcp_protector
    bl      _abort
    #else
    bl      aco_funcp_protector
    bl      abort
    #endif

    ret
#else
    #error "platform not support"
#endif
