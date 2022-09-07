#ifndef NATURE_SRC_ASSEMBLER_X86_64_REGISTER_H_
#define NATURE_SRC_ASSEMBLER_X86_64_REGISTER_H_

#include "asm.h"
#include "utils/table.h"

table *x86_64_regs_table; // key index_size

asm_operand_register_t *rax;
asm_operand_register_t *rcx;
asm_operand_register_t *rdx;
asm_operand_register_t *rbx;
asm_operand_register_t *rsp;
asm_operand_register_t *rbp;
asm_operand_register_t *rsi;
asm_operand_register_t *rdi;
asm_operand_register_t *r8;
asm_operand_register_t *r9;
asm_operand_register_t *r10;
asm_operand_register_t *r11;
asm_operand_register_t *r12;
asm_operand_register_t *r13;
asm_operand_register_t *r14;
asm_operand_register_t *r15;

asm_operand_register_t *eax;
asm_operand_register_t *ecx;
asm_operand_register_t *edx;
asm_operand_register_t *ebx;
asm_operand_register_t *esp;
asm_operand_register_t *ebp;
asm_operand_register_t *esi;
asm_operand_register_t *edi;
asm_operand_register_t *r8d;
asm_operand_register_t *r9d;
asm_operand_register_t *r10d;
asm_operand_register_t *r11d;
asm_operand_register_t *r12d;
asm_operand_register_t *r13d;
asm_operand_register_t *r14d;
asm_operand_register_t *r15d;

asm_operand_register_t *ax;
asm_operand_register_t *cx;
asm_operand_register_t *dx;
asm_operand_register_t *bx;
asm_operand_register_t *sp;
asm_operand_register_t *bp;
asm_operand_register_t *si;
asm_operand_register_t *di;
asm_operand_register_t *r8w;
asm_operand_register_t *r9w;
asm_operand_register_t *r10w;
asm_operand_register_t *r11w;
asm_operand_register_t *r12w;
asm_operand_register_t *r13w;
asm_operand_register_t *r14w;
asm_operand_register_t *r15w;

asm_operand_register_t *al;
asm_operand_register_t *cl;
asm_operand_register_t *dl;
asm_operand_register_t *bl;
asm_operand_register_t *spl;
asm_operand_register_t *bpl;
asm_operand_register_t *sil;
asm_operand_register_t *dil;
asm_operand_register_t *r8b;
asm_operand_register_t *r9b;
asm_operand_register_t *r10b;
asm_operand_register_t *r11b;
asm_operand_register_t *r12b;
asm_operand_register_t *r13b;
asm_operand_register_t *r14b;
asm_operand_register_t *r15b;

asm_operand_register_t *ah;
asm_operand_register_t *ch;
asm_operand_register_t *dh;
asm_operand_register_t *bh;

asm_operand_register_t *xmm0;
asm_operand_register_t *xmm1;
asm_operand_register_t *xmm2;
asm_operand_register_t *xmm3;
asm_operand_register_t *xmm4;
asm_operand_register_t *xmm5;
asm_operand_register_t *xmm6;
asm_operand_register_t *xmm7;
asm_operand_register_t *xmm8;
asm_operand_register_t *xmm9;
asm_operand_register_t *xmm10;
asm_operand_register_t *xmm11;
asm_operand_register_t *xmm12;
asm_operand_register_t *xmm13;
asm_operand_register_t *xmm14;
asm_operand_register_t *xmm15;

asm_operand_register_t *ymm0;
asm_operand_register_t *ymm1;
asm_operand_register_t *ymm2;
asm_operand_register_t *ymm3;
asm_operand_register_t *ymm4;
asm_operand_register_t *ymm5;
asm_operand_register_t *ymm6;
asm_operand_register_t *ymm7;
asm_operand_register_t *ymm8;
asm_operand_register_t *ymm9;
asm_operand_register_t *ymm10;
asm_operand_register_t *ymm11;
asm_operand_register_t *ymm12;
asm_operand_register_t *ymm13;
asm_operand_register_t *ymm14;
asm_operand_register_t *ymm15;

asm_operand_register_t *zmm0;
asm_operand_register_t *zmm1;
asm_operand_register_t *zmm2;
asm_operand_register_t *zmm3;
asm_operand_register_t *zmm4;
asm_operand_register_t *zmm5;
asm_operand_register_t *zmm6;
asm_operand_register_t *zmm7;
asm_operand_register_t *zmm8;
asm_operand_register_t *zmm9;
asm_operand_register_t *zmm10;
asm_operand_register_t *zmm11;
asm_operand_register_t *zmm12;
asm_operand_register_t *zmm13;
asm_operand_register_t *zmm14;
asm_operand_register_t *zmm15;

void x86_64_register_init();

asm_operand_register_t *x86_64_register_operand_new(char *name, uint8_t index, uint8_t size);

asm_operand_register_t *x86_64_register_find(uint8_t index, uint8_t size);

#endif //NATURE_SRC_ASSEMBLER_X86_64_REGISTER_H_
