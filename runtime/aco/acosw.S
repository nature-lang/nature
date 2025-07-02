// gcc 支持 .S 文件预处理

.text
.globl acosw
#if defined(__DARWIN)
#else
.type  acosw, @function
#endif

#if !defined(__ARM64) && !defined(__RISCV64)
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


#elif defined(__RISCV64)
     // a0 - from_co | a1 - to_co

     // Save callee-saved registers to from_co
     sd      s0,  0(a0)      // s0/fp
     sd      s1,  8(a0)      // s1
     sd      s2,  16(a0)     // s2
     sd      s3,  24(a0)     // s3
     sd      s4,  32(a0)     // s4
     sd      s5,  40(a0)     // s5
     sd      s6,  48(a0)     // s6
     sd      s7,  56(a0)     // s7
     sd      s8,  64(a0)     // s8
     sd      s9,  72(a0)     // s9
     sd      s10, 80(a0)     // s10
     sd      s11, 88(a0)     // s11
     sd      ra,  96(a0)     // return address
     sd      sp,  104(a0)    // stack pointer
#ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
     frcsr   t0              // read floating-point control and status register
     sd      t0,  112(a0)    // fcsr
#endif

     // Restore callee-saved registers from to_co
     ld      s0,  0(a1)      // s0/fp
     ld      s1,  8(a1)      // s1
     ld      s2,  16(a1)     // s2
     ld      s3,  24(a1)     // s3
     ld      s4,  32(a1)     // s4
     ld      s5,  40(a1)     // s5
     ld      s6,  48(a1)     // s6
     ld      s7,  56(a1)     // s7
     ld      s8,  64(a1)     // s8
     ld      s9,  72(a1)     // s9
     ld      s10, 80(a1)     // s10
     ld      s11, 88(a1)     // s11
     ld      ra,  96(a1)     // return address
     ld      sp,  104(a1)    // stack pointer
#ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
     ld      t0,  112(a1)    // fcsr
     fscsr   t0              // write floating-point control and status register
#endif

     jr      ra              // jump to return address

     
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
.globl _assist_preempt_yield
_assist_preempt_yield:
#else
.globl assist_preempt_yield
.type  assist_preempt_yield, @function
assist_preempt_yield:
#endif
#ifdef __AMD64
    push rbp
    mov rbp, rsp
    pushfq
    sub rsp, 376 // 376 + ret_addr + rbp + pushfd = 400, 16 对齐

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
    // mov rdi, rbp
    // mov rsi, [rbp + 8] // +8 是跳过 prev rbp, 此时栈中存储的值就是 ret addr 的值
    // call SYM(debug_ret)

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

    add rsp, 376

    popfq // 从栈顶弹出一个值到 RFLAGS 寄存器中，与之前的 pushfq 对应

    pop rbp // 从栈顶弹出一个值到rbp寄存器中, 与之前的 push rbp dvyk

    ret

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
    // add sp, sp, #1032          // 恢复栈指针
    ret                        // 返回到被中断的位置, 基于 x30 中的值进行返回


#elif defined(__RISCV64)
    // Save frame pointer and return address
    addi    sp, sp, -16
    sd      s0, 0(sp)
    sd      ra, 8(sp)
    addi    s0, sp, 16
    
    // Allocate space for all registers (32 general + 32 floating point)
    addi    sp, sp, -512
    
    // Save general-purpose registers
    sd      x1,  0(sp)      // ra
    sd      x2,  8(sp)      // sp (will be updated)
    sd      x3,  16(sp)     // gp
    sd      x4,  24(sp)     // tp
    sd      x5,  32(sp)     // t0
    sd      x6,  40(sp)     // t1
    sd      x7,  48(sp)     // t2
    sd      x8,  56(sp)     // s0/fp
    sd      x9,  64(sp)     // s1
    sd      x10, 72(sp)     // a0
    sd      x11, 80(sp)     // a1
    sd      x12, 88(sp)     // a2
    sd      x13, 96(sp)     // a3
    sd      x14, 104(sp)    // a4
    sd      x15, 112(sp)    // a5
    sd      x16, 120(sp)    // a6
    sd      x17, 128(sp)    // a7
    sd      x18, 136(sp)    // s2
    sd      x19, 144(sp)    // s3
    sd      x20, 152(sp)    // s4
    sd      x21, 160(sp)    // s5
    sd      x22, 168(sp)    // s6
    sd      x23, 176(sp)    // s7
    sd      x24, 184(sp)    // s8
    sd      x25, 192(sp)    // s9
    sd      x26, 200(sp)    // s10
    sd      x27, 208(sp)    // s11
    sd      x28, 216(sp)    // t3
    sd      x29, 224(sp)    // t4
    sd      x30, 232(sp)    // t5
    sd      x31, 240(sp)    // t6
    
    // Save floating-point registers
    fsd     f0,  248(sp)
    fsd     f1,  256(sp)
    fsd     f2,  264(sp)
    fsd     f3,  272(sp)
    fsd     f4,  280(sp)
    fsd     f5,  288(sp)
    fsd     f6,  296(sp)
    fsd     f7,  304(sp)
    fsd     f8,  312(sp)
    fsd     f9,  320(sp)
    fsd     f10, 328(sp)
    fsd     f11, 336(sp)
    fsd     f12, 344(sp)
    fsd     f13, 352(sp)
    fsd     f14, 360(sp)
    fsd     f15, 368(sp)
    fsd     f16, 376(sp)
    fsd     f17, 384(sp)
    fsd     f18, 392(sp)
    fsd     f19, 400(sp)
    fsd     f20, 408(sp)
    fsd     f21, 416(sp)
    fsd     f22, 424(sp)
    fsd     f23, 432(sp)
    fsd     f24, 440(sp)
    fsd     f25, 448(sp)
    fsd     f26, 456(sp)
    fsd     f27, 464(sp)
    fsd     f28, 472(sp)
    fsd     f29, 480(sp)
    fsd     f30, 488(sp)
    fsd     f31, 496(sp)
    
    // Update saved sp to point to the correct location
    addi    t0, s0, 0
    sd      t0, 8(sp)
    
    call    SYM(co_preempt_yield)
    
    // Restore floating-point registers
    fld     f31, 496(sp)
    fld     f30, 488(sp)
    fld     f29, 480(sp)
    fld     f28, 472(sp)
    fld     f27, 464(sp)
    fld     f26, 456(sp)
    fld     f25, 448(sp)
    fld     f24, 440(sp)
    fld     f23, 432(sp)
    fld     f22, 424(sp)
    fld     f21, 416(sp)
    fld     f20, 408(sp)
    fld     f19, 400(sp)
    fld     f18, 392(sp)
    fld     f17, 384(sp)
    fld     f16, 376(sp)
    fld     f15, 368(sp)
    fld     f14, 360(sp)
    fld     f13, 352(sp)
    fld     f12, 344(sp)
    fld     f11, 336(sp)
    fld     f10, 328(sp)
    fld     f9,  320(sp)
    fld     f8,  312(sp)
    fld     f7,  304(sp)
    fld     f6,  296(sp)
    fld     f5,  288(sp)
    fld     f4,  280(sp)
    fld     f3,  272(sp)
    fld     f2,  264(sp)
    fld     f1,  256(sp)
    fld     f0,  248(sp)
    
    // Restore general-purpose registers (except sp and s0)
    ld      x31, 240(sp)    // t6
    ld      x30, 232(sp)    // t5
    ld      x29, 224(sp)    // t4
    ld      x28, 216(sp)    // t3
    ld      x27, 208(sp)    // s11
    ld      x26, 200(sp)    // s10
    ld      x25, 192(sp)    // s9
    ld      x24, 184(sp)    // s8
    ld      x23, 176(sp)    // s7
    ld      x22, 168(sp)    // s6
    ld      x21, 160(sp)    // s5
    ld      x20, 152(sp)    // s4
    ld      x19, 144(sp)    // s3
    ld      x18, 136(sp)    // s2
    ld      x17, 128(sp)    // a7
    ld      x16, 120(sp)    // a6
    ld      x15, 112(sp)    // a5
    ld      x14, 104(sp)    // a4
    ld      x13, 96(sp)     // a3
    ld      x12, 88(sp)     // a2
    ld      x11, 80(sp)     // a1
    ld      x10, 72(sp)     // a0
    ld      x9,  64(sp)     // s1
    // x8 (s0) will be restored later
    ld      x7,  48(sp)     // t2
    ld      x6,  40(sp)     // t1
    ld      x5,  32(sp)     // t0
    ld      x4,  24(sp)     // tp
    ld      x3,  16(sp)     // gp
    // x2 (sp) will be restored by frame unwinding
    ld      x1,  0(sp)      // ra
    
    // Restore stack pointer and frame
    addi    sp, sp, 512
    ld      s0, 0(sp)
    ld      ra, 8(sp)
    addi    sp, sp, 16
    
    ret

#else
#error "platform not support"
#endif





.globl aco_save_reg
#if defined(__DARWIN)
#else
.type  aco_save_reg, @function
#endif

#if !defined(__ARM64) && !defined(__RISCV64)
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

#elif defined(__RISCV64)
    // a0 contains target address
    // Save callee-saved registers
    sd      s0,  0(a0)      // s0/fp
    sd      s1,  8(a0)      // s1
    sd      s2,  16(a0)     // s2
    sd      s3,  24(a0)     // s3
    sd      s4,  32(a0)     // s4
    sd      s5,  40(a0)     // s5
    sd      s6,  48(a0)     // s6
    sd      s7,  56(a0)     // s7
    sd      s8,  64(a0)     // s8
    sd      s9,  72(a0)     // s9
    sd      s10, 80(a0)     // s10
    sd      s11, 88(a0)     // s11
    sd      ra,  96(a0)     // return address
    sd      sp,  104(a0)    // stack pointer
#ifndef ACO_CONFIG_SHARE_FPU_MXCSR_ENV
    frcsr   t0              // read floating-point control and status register
    sd      t0,  112(a0)    // fcsr
#endif
    ret

#else
    #error "platform not support"
#endif


.globl aco_save_fpucw_mxcsr
#if defined(__DARWIN)
#else
.type  aco_save_fpucw_mxcsr, @function
#endif
#if !defined(__ARM64) && !defined(__RISCV64)
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
#elif __RISCV64
    frcsr   t0              // read floating-point control and status register
    sd      t0, 0(a0)       // store fcsr
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

#if !defined(__ARM64) && !defined(__RISCV64)
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
#elif defined(__RISCV64)
    // Align stack pointer to 16-byte boundary
    andi    sp, sp, -16
#if defined(__DARWIN)
    call    _aco_funcp_protector
    call    _abort
#else
    call    aco_funcp_protector
    call    abort
#endif
    ret
#else
    #error "platform not support"
#endif
