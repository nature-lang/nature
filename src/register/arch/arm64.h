#ifndef NATURE_REGISTER_ARM64_H
#define NATURE_REGISTER_ARM64_H

#include "src/types.h"

// 通用寄存器
extern reg_t *x0, *x1, *x2, *x3, *x4, *x5, *x6, *x7;
extern reg_t *x8, *x9, *x10, *x11, *x12, *x13, *x14, *x15;
extern reg_t *x16, *x17, *x18, *x19, *x20, *x21, *x22, *x23;
extern reg_t *x24, *x25, *x26, *x27, *x28, *x29, *x30; // x29 = fp, x30 = lr

// 32位别名定义
extern reg_t *w0, *w1, *w2, *w3, *w4, *w5, *w6, *w7;
extern reg_t *w8, *w9, *w10, *w11, *w12, *w13, *w14, *w15;
extern reg_t *w16, *w17, *w18, *w19, *w20, *w21, *w22, *w23;
extern reg_t *w24, *w25, *w26, *w27, *w28, *w29, *w30;

// 特殊寄存器
extern reg_t *fp;
extern reg_t *lr;
extern reg_t *sp; // 栈指针
extern reg_t *pc; // 程序计数器
extern reg_t *xzr; // 零寄存器
extern reg_t *wzr; // 32位零寄存器

// 浮点寄存器
extern reg_t *v0, *v1, *v2, *v3, *v4, *v5, *v6, *v7;
extern reg_t *v8, *v9, *v10, *v11, *v12, *v13, *v14, *v15;
extern reg_t *v16, *v17, *v18, *v19, *v20, *v21, *v22, *v23;
extern reg_t *v24, *v25, *v26, *v27, *v28, *v29, *v30, *v31;

// 64位 浮点寄存器
extern reg_t *d0, *d1, *d2, *d3, *d4, *d5, *d6, *d7;
extern reg_t *d8, *d9, *d10, *d11, *d12, *d13, *d14, *d15;
extern reg_t *d16, *d17, *d18, *d19, *d20, *d21, *d22, *d23;
extern reg_t *d24, *d25, *d26, *d27, *d28, *d29, *d30, *d31;

// 32位 浮点寄存器
extern reg_t *s0, *s1, *s2, *s3, *s4, *s5, *s6, *s7;
extern reg_t *s8, *s9, *s10, *s11, *s12, *s13, *s14, *s15;
extern reg_t *s16, *s17, *s18, *s19, *s20, *s21, *s22, *s23;
extern reg_t *s24, *s25, *s26, *s27, *s28, *s29, *s30, *s31;

#define ARM64_ALLOC_REG_COUNT 27 + 31;
#define DARWIN_ARM64_ALLOC_REG_COUNT 26 + 31;

#define ARM64_STACK_ALIGN_SIZE 16

void arm64_reg_init();

#endif //NATURE_ARM64_H
