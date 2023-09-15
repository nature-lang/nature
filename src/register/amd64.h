#ifndef NATURE_REGISTER_AMD64_H
#define NATURE_REGISTER_AMD64_H

#include "register.h"
#include "src/lir.h"

extern reg_t *rax;
extern reg_t *rcx;
extern reg_t *rdx;
extern reg_t *rbx;
extern reg_t *rsp;
extern reg_t *rbp;
extern reg_t *rsi;
extern reg_t *rdi;
extern reg_t *r8;
extern reg_t *r9;
extern reg_t *r10;
extern reg_t *r11;
extern reg_t *r12;
extern reg_t *r13;
extern reg_t *r14;
extern reg_t *r15;

extern reg_t *eax;
extern reg_t *ecx;
extern reg_t *edx;
extern reg_t *ebx;
extern reg_t *esp;
extern reg_t *ebp;
extern reg_t *esi;
extern reg_t *edi;
extern reg_t *r8d;
extern reg_t *r9d;
extern reg_t *r10d;
extern reg_t *r11d;
extern reg_t *r12d;
extern reg_t *r13d;
extern reg_t *r14d;
extern reg_t *r15d;

extern reg_t *ax;
extern reg_t *cx;
extern reg_t *dx;
extern reg_t *bx;
extern reg_t *sp;
extern reg_t *bp;
extern reg_t *si;
extern reg_t *di;
extern reg_t *r8w;
extern reg_t *r9w;
extern reg_t *r10w;
extern reg_t *r11w;
extern reg_t *r12w;
extern reg_t *r13w;
extern reg_t *r14w;
extern reg_t *r15w;

extern reg_t *al;
extern reg_t *cl;
extern reg_t *dl;
extern reg_t *bl;
extern reg_t *spl;
extern reg_t *bpl;
extern reg_t *sil;
extern reg_t *dil;
extern reg_t *r8b;
extern reg_t *r9b;
extern reg_t *r10b;
extern reg_t *r11b;
extern reg_t *r12b;
extern reg_t *r13b;
extern reg_t *r14b;
extern reg_t *r15b;

extern reg_t *ah;
extern reg_t *ch;
extern reg_t *dh;
extern reg_t *bh;

extern reg_t *xmm0s64;
extern reg_t *xmm1s64;
extern reg_t *xmm2s64;
extern reg_t *xmm3s64;
extern reg_t *xmm4s64;
extern reg_t *xmm5s64;
extern reg_t *xmm6s64;
extern reg_t *xmm7s64;
extern reg_t *xmm8s64;
extern reg_t *xmm9s64;
extern reg_t *xmm10s64;
extern reg_t *xmm11s64;
extern reg_t *xmm12s64;
extern reg_t *xmm13s64;
extern reg_t *xmm14s64;
extern reg_t *xmm15s64;

extern reg_t *xmm0s32;
extern reg_t *xmm1s32;
extern reg_t *xmm2s32;
extern reg_t *xmm3s32;
extern reg_t *xmm4s32;
extern reg_t *xmm5s32;
extern reg_t *xmm6s32;
extern reg_t *xmm7s32;
extern reg_t *xmm8s32;
extern reg_t *xmm9s32;
extern reg_t *xmm10s32;
extern reg_t *xmm11s32;
extern reg_t *xmm12s32;
extern reg_t *xmm13s32;
extern reg_t *xmm14s32;
extern reg_t *xmm15s32;

extern reg_t *xmm0;
extern reg_t *xmm1;
extern reg_t *xmm2;
extern reg_t *xmm3;
extern reg_t *xmm4;
extern reg_t *xmm5;
extern reg_t *xmm6;
extern reg_t *xmm7;
extern reg_t *xmm8;
extern reg_t *xmm9;
extern reg_t *xmm10;
extern reg_t *xmm11;
extern reg_t *xmm12;
extern reg_t *xmm13;
extern reg_t *xmm14;
extern reg_t *xmm15;

extern reg_t *ymm0;
extern reg_t *ymm1;
extern reg_t *ymm2;
extern reg_t *ymm3;
extern reg_t *ymm4;
extern reg_t *ymm5;
extern reg_t *ymm6;
extern reg_t *ymm7;
extern reg_t *ymm8;
extern reg_t *ymm9;
extern reg_t *ymm10;
extern reg_t *ymm11;
extern reg_t *ymm12;
extern reg_t *ymm13;
extern reg_t *ymm14;
extern reg_t *ymm15;

extern reg_t *zmm0;
extern reg_t *zmm1;
extern reg_t *zmm2;
extern reg_t *zmm3;
extern reg_t *zmm4;
extern reg_t *zmm5;
extern reg_t *zmm6;
extern reg_t *zmm7;
extern reg_t *zmm8;
extern reg_t *zmm9;
extern reg_t *zmm10;
extern reg_t *zmm11;
extern reg_t *zmm12;
extern reg_t *zmm13;
extern reg_t *zmm14;
extern reg_t *zmm15;

#define AMD64_ALLOC_INT_REG_COUNT 14;
#define AMD64_ALLOC_FLOAT_REG_COUNT 16;
#define AMD64_ALLOC_REG_COUNT 14+16;
#define STACK_ALIGN_SIZE 16


reg_t *amd64_fn_param_next_reg(uint8_t used[2], type_kind kind);

#endif //NATURE_AMD64_H
