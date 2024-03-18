// gcc 支持 .S 文件预处理

.text
.globl acosw
#if defined(__APPLE__)
#else
.type  acosw, @function
#endif
.intel_syntax noprefix
acosw:
#ifdef __x86_64__
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
#else
    #error "platform not support"
#endif


// ----------------------------------------------------------------------------------------
.globl async_preempt
#if defined(__APPLE__)
#else
.type  async_preempt, @function
#endif
async_preempt:
#ifdef __x86_64__
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
#if defined(__APPLE__)
#ifndef hasAVX
    cmp byte ptr [internal/cpu.X86 + const_offsetX86HasAVX], 0
    je .Lno_avx
#endif
    vzeroupper
    .Lno_avx
#endif
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
    call co_preempt_yield


    // debug (多的不能确定，但是这里肯定是 0 了，毕竟ret 地址被覆盖了，可关键是，栈空间还是会被污染呢)
    mov rdi, rbp
    mov rsi, [rbp + 8] // +8 是跳过 prev rbp, 此时栈中存储的值就是 ret addr 的值
    call debug_ret

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
#else
#error "platform not support"
#endif




.globl aco_save_reg
#if defined(__APPLE__)
#else
.type  aco_save_reg, @function
#endif
.intel_syntax noprefix
aco_save_reg:

#ifdef __x86_64__
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
#else
    #error "platform not support"
#endif

.globl aco_save_fpucw_mxcsr
#if defined(__APPLE__)
#else
.type  aco_save_fpucw_mxcsr, @function
#endif
.intel_syntax noprefix

aco_save_fpucw_mxcsr:
#ifdef __x86_64__
    fnstcw  WORD PTR  [rdi]
    stmxcsr DWORD PTR [rdi+0x4]
    ret
#else
    #error "platform not support"
#endif

#if defined(__APPLE__)
.globl _abort
.globl _aco_funcp_protector
#else
.globl abort
.globl aco_funcp_protector
#endif

.globl aco_funcp_protector_asm
#if defined(__APPLE__)
#else
.type  aco_funcp_protector_asm, @function
#endif
.intel_syntax noprefix
aco_funcp_protector_asm:
#ifdef __x86_64__
            and     rsp,0xfffffffffffffff0
    #if defined(__APPLE__)
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
