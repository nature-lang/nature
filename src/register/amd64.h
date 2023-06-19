#ifndef NATURE_REGISTER_AMD64_H
#define NATURE_REGISTER_AMD64_H

#include "register.h"
#include "src/lir.h"

reg_t *rax;
reg_t *rcx;
reg_t *rdx;
reg_t *rbx;
reg_t *rsp;
reg_t *rbp;
reg_t *rsi;
reg_t *rdi;
reg_t *r8;
reg_t *r9;
reg_t *r10;
reg_t *r11;
reg_t *r12;
reg_t *r13;
reg_t *r14;
reg_t *r15;

reg_t *eax;
reg_t *ecx;
reg_t *edx;
reg_t *ebx;
reg_t *esp;
reg_t *ebp;
reg_t *esi;
reg_t *edi;
reg_t *r8d;
reg_t *r9d;
reg_t *r10d;
reg_t *r11d;
reg_t *r12d;
reg_t *r13d;
reg_t *r14d;
reg_t *r15d;

reg_t *ax;
reg_t *cx;
reg_t *dx;
reg_t *bx;
reg_t *sp;
reg_t *bp;
reg_t *si;
reg_t *di;
reg_t *r8w;
reg_t *r9w;
reg_t *r10w;
reg_t *r11w;
reg_t *r12w;
reg_t *r13w;
reg_t *r14w;
reg_t *r15w;

reg_t *al;
reg_t *cl;
reg_t *dl;
reg_t *bl;
reg_t *spl;
reg_t *bpl;
reg_t *sil;
reg_t *dil;
reg_t *r8b;
reg_t *r9b;
reg_t *r10b;
reg_t *r11b;
reg_t *r12b;
reg_t *r13b;
reg_t *r14b;
reg_t *r15b;

reg_t *ah;
reg_t *ch;
reg_t *dh;
reg_t *bh;

reg_t *xmm0s64;
reg_t *xmm1s64;
reg_t *xmm2s64;
reg_t *xmm3s64;
reg_t *xmm4s64;
reg_t *xmm5s64;
reg_t *xmm6s64;
reg_t *xmm7s64;
reg_t *xmm8s64;
reg_t *xmm9s64;
reg_t *xmm10s64;
reg_t *xmm11s64;
reg_t *xmm12s64;
reg_t *xmm13s64;
reg_t *xmm14s64;
reg_t *xmm15s64;

reg_t *xmm0s32;
reg_t *xmm1s32;
reg_t *xmm2s32;
reg_t *xmm3s32;
reg_t *xmm4s32;
reg_t *xmm5s32;
reg_t *xmm6s32;
reg_t *xmm7s32;
reg_t *xmm8s32;
reg_t *xmm9s32;
reg_t *xmm10s32;
reg_t *xmm11s32;
reg_t *xmm12s32;
reg_t *xmm13s32;
reg_t *xmm14s32;
reg_t *xmm15s32;

reg_t *xmm0;
reg_t *xmm1;
reg_t *xmm2;
reg_t *xmm3;
reg_t *xmm4;
reg_t *xmm5;
reg_t *xmm6;
reg_t *xmm7;
reg_t *xmm8;
reg_t *xmm9;
reg_t *xmm10;
reg_t *xmm11;
reg_t *xmm12;
reg_t *xmm13;
reg_t *xmm14;
reg_t *xmm15;

reg_t *ymm0;
reg_t *ymm1;
reg_t *ymm2;
reg_t *ymm3;
reg_t *ymm4;
reg_t *ymm5;
reg_t *ymm6;
reg_t *ymm7;
reg_t *ymm8;
reg_t *ymm9;
reg_t *ymm10;
reg_t *ymm11;
reg_t *ymm12;
reg_t *ymm13;
reg_t *ymm14;
reg_t *ymm15;

reg_t *zmm0;
reg_t *zmm1;
reg_t *zmm2;
reg_t *zmm3;
reg_t *zmm4;
reg_t *zmm5;
reg_t *zmm6;
reg_t *zmm7;
reg_t *zmm8;
reg_t *zmm9;
reg_t *zmm10;
reg_t *zmm11;
reg_t *zmm12;
reg_t *zmm13;
reg_t *zmm14;
reg_t *zmm15;

#define AMD64_ALLOC_INT_REG_COUNT 14;
#define AMD64_ALLOC_FLOAT_REG_COUNT 16;
#define AMD64_ALLOC_REG_COUNT 14+16;
#define ALIGN_SIZE 16


reg_t *amd64_fn_param_next_reg(uint8_t used[2], type_kind kind);

#endif //NATURE_AMD64_H
