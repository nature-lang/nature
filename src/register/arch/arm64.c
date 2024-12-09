#include "arm64.h"
#include "utils/type.h"
#include "utils/slice.h"
#include "src/register/register.h"
#include <stdio.h>

// 通用寄存器定义
reg_t *x0, *x1, *x2, *x3, *x4, *x5, *x6, *x7;
reg_t *x8, *x9, *x10, *x11, *x12, *x13, *x14, *x15;
reg_t *x16, *x17, *x18, *x19, *x20, *x21, *x22, *x23;
reg_t *x24, *x25, *x26, *x27, *x28, *x29, *x30;

// 32位别名定义
reg_t *w0, *w1, *w2, *w3, *w4, *w5, *w6, *w7;
reg_t *w8, *w9, *w10, *w11, *w12, *w13, *w14, *w15;
reg_t *w16, *w17, *w18, *w19, *w20, *w21, *w22, *w23;
reg_t *w24, *w25, *w26, *w27, *w28, *w29, *w30;

// 特殊寄存器定义
reg_t *fp;
reg_t *lr;
reg_t *sp;
reg_t *pc;
reg_t *xzr;
reg_t *wzr;

// 浮点寄存器定义
reg_t *v0, *v1, *v2, *v3, *v4, *v5, *v6, *v7;
reg_t *v8, *v9, *v10, *v11, *v12, *v13, *v14, *v15;
reg_t *v16, *v17, *v18, *v19, *v20, *v21, *v22, *v23;
reg_t *v24, *v25, *v26, *v27, *v28, *v29, *v30, *v31;

// 64位 浮点寄存器
reg_t *d0, *d1, *d2, *d3, *d4, *d5, *d6, *d7;
reg_t *d8, *d9, *d10, *d11, *d12, *d13, *d14, *d15;
reg_t *d16, *d17, *d18, *d19, *d20, *d21, *d22, *d23;
reg_t *d24, *d25, *d26, *d27, *d28, *d29, *d30, *d31;

// 32位 浮点寄存器
reg_t *s0, *s1, *s2, *s3, *s4, *s5, *s6, *s7;
reg_t *s8, *s9, *s10, *s11, *s12, *s13, *s14, *s15;
reg_t *s16, *s17, *s18, *s19, *s20, *s21, *s22, *s23;
reg_t *s24, *s25, *s26, *s27, *s28, *s29, *s30, *s31;


void arm64_reg_init() {
    // 通用寄存器定义
    x0 = reg_new("x0", 0, LIR_FLAG_ALLOC_INT, QWORD, 1);
    x1 = reg_new("x1", 1, LIR_FLAG_ALLOC_INT, QWORD, 2);
    x2 = reg_new("x2", 2, LIR_FLAG_ALLOC_INT, QWORD, 3);
    x3 = reg_new("x3", 3, LIR_FLAG_ALLOC_INT, QWORD, 4);
    x4 = reg_new("x4", 4, LIR_FLAG_ALLOC_INT, QWORD, 5);
    x5 = reg_new("x5", 5, LIR_FLAG_ALLOC_INT, QWORD, 6);
    x6 = reg_new("x6", 6, LIR_FLAG_ALLOC_INT, QWORD, 7);
    x7 = reg_new("x7", 7, LIR_FLAG_ALLOC_INT, QWORD, 8);
    x8 = reg_new("x8", 8, LIR_FLAG_ALLOC_INT, QWORD, 9);
    x9 = reg_new("x9", 9, LIR_FLAG_ALLOC_INT, QWORD, 10);
    x10 = reg_new("x10", 10, LIR_FLAG_ALLOC_INT, QWORD, 11);
    x11 = reg_new("x11", 11, LIR_FLAG_ALLOC_INT, QWORD, 12);
    x12 = reg_new("x12", 12, LIR_FLAG_ALLOC_INT, QWORD, 13);
    x13 = reg_new("x13", 13, LIR_FLAG_ALLOC_INT, QWORD, 14);
    x14 = reg_new("x14", 14, LIR_FLAG_ALLOC_INT, QWORD, 15);
    x15 = reg_new("x15", 15, LIR_FLAG_ALLOC_INT, QWORD, 16);
    x16 = reg_new("x16", 16, LIR_FLAG_ALLOC_INT, QWORD, 0); // native 保留临时寄存器
    x17 = reg_new("x17", 17, LIR_FLAG_ALLOC_INT, QWORD, 17);
    x18 = reg_new("x18", 18, LIR_FLAG_ALLOC_INT, QWORD, 0); // macos 保留
    x19 = reg_new("x19", 19, LIR_FLAG_ALLOC_INT, QWORD, 18);
    x20 = reg_new("x20", 20, LIR_FLAG_ALLOC_INT, QWORD, 19);
    x21 = reg_new("x21", 21, LIR_FLAG_ALLOC_INT, QWORD, 20);
    x22 = reg_new("x22", 22, LIR_FLAG_ALLOC_INT, QWORD, 21);
    x23 = reg_new("x23", 23, LIR_FLAG_ALLOC_INT, QWORD, 22);
    x24 = reg_new("x24", 24, LIR_FLAG_ALLOC_INT, QWORD, 23);
    x25 = reg_new("x25", 25, LIR_FLAG_ALLOC_INT, QWORD, 24);
    x26 = reg_new("x26", 26, LIR_FLAG_ALLOC_INT, QWORD, 25);
    x27 = reg_new("x27", 27, LIR_FLAG_ALLOC_INT, QWORD, 26);
    x28 = reg_new("x28", 28, LIR_FLAG_ALLOC_INT, QWORD, 27);
    x29 = reg_new("x29", 29, 0, QWORD, 0); // FP, 不参与分配
    x30 = reg_new("x30", 30, 0, QWORD, 0); // LR, 不参与分配
    fp = x29;
    lr = x30;


    // 32位寄存器初始化
    w0 = reg_new("w0", 0, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w1 = reg_new("w1", 1, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w2 = reg_new("w2", 2, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w3 = reg_new("w3", 3, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w4 = reg_new("w4", 4, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w5 = reg_new("w5", 5, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w6 = reg_new("w6", 6, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w7 = reg_new("w7", 7, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w8 = reg_new("w8", 8, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w9 = reg_new("w9", 9, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w10 = reg_new("w10", 10, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w11 = reg_new("w11", 11, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w12 = reg_new("w12", 12, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w13 = reg_new("w13", 13, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w14 = reg_new("w14", 14, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w15 = reg_new("w15", 15, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w16 = reg_new("w16", 16, 0, DWORD, 0); // IP0, 不参与分配
    w17 = reg_new("w17", 17, 0, DWORD, 0); // IP1, 不参与分配
    w18 = reg_new("w18", 18, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w19 = reg_new("w19", 19, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w20 = reg_new("w20", 20, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w21 = reg_new("w21", 21, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w22 = reg_new("w22", 22, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w23 = reg_new("w23", 23, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w24 = reg_new("w24", 24, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w25 = reg_new("w25", 25, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w26 = reg_new("w26", 26, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w27 = reg_new("w27", 27, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w28 = reg_new("w28", 28, LIR_FLAG_ALLOC_INT, DWORD, 0);
    w29 = reg_new("w29", 29, 0, DWORD, 0); // FP, 不参与分配
    w30 = reg_new("w30", 30, 0, DWORD, 0); // LR, 不参与分配

    // 初始化特殊寄存器
    sp = reg_new("sp", 31, LIR_FLAG_ALLOC_INT, QWORD, 0);
    pc = reg_new("pc", 32, 0, QWORD, 0);
    xzr = reg_new("xzr", 31, 0, QWORD, 0);
    wzr = reg_new("wzr", 31, 0, DWORD, 0);

    int f_index = 27;

    // 浮点寄存器
    v0 = reg_new("v0", 0, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 1);
    v1 = reg_new("v1", 1, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 2);
    v2 = reg_new("v2", 2, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 3);
    v3 = reg_new("v3", 3, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 4);
    v4 = reg_new("v4", 4, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 5);
    v5 = reg_new("v5", 5, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 6);
    v6 = reg_new("v6", 6, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 7);
    v7 = reg_new("v7", 7, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 8);
    v8 = reg_new("v8", 8, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 9);
    v9 = reg_new("v9", 9, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 10);
    v10 = reg_new("v10", 10, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 11);
    v11 = reg_new("v11", 11, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 12);
    v12 = reg_new("v12", 12, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 13);
    v13 = reg_new("v13", 13, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 14);
    v14 = reg_new("v14", 14, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 15);
    v15 = reg_new("v15", 15, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 16);
    v16 = reg_new("v16", 16, LIR_FLAG_ALLOC_FLOAT, OWORD, 0); // 预留给 native 使用
    v17 = reg_new("v17", 17, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 17);
    v18 = reg_new("v18", 18, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 18);
    v19 = reg_new("v19", 19, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 19);
    v20 = reg_new("v20", 20, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 20);
    v21 = reg_new("v21", 21, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 21);
    v22 = reg_new("v22", 22, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 22);
    v23 = reg_new("v23", 23, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 23);
    v24 = reg_new("v24", 24, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 24);
    v25 = reg_new("v25", 25, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 25);
    v26 = reg_new("v26", 26, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 26);
    v27 = reg_new("v27", 27, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 27);
    v28 = reg_new("v28", 28, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 28);
    v29 = reg_new("v29", 29, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 29);
    v30 = reg_new("v30", 30, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 30);
    v31 = reg_new("v31", 31, LIR_FLAG_ALLOC_FLOAT, OWORD, f_index + 31);

    // 64位 浮点寄存器(不需要处理 f_index)
    d0 = reg_new("d0", 0, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d1 = reg_new("d1", 1, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d2 = reg_new("d2", 2, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d3 = reg_new("d3", 3, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d4 = reg_new("d4", 4, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d5 = reg_new("d5", 5, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d6 = reg_new("d6", 6, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d7 = reg_new("d7", 7, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d8 = reg_new("d8", 8, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d9 = reg_new("d9", 9, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d10 = reg_new("d10", 10, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d11 = reg_new("d11", 11, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d12 = reg_new("d12", 12, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d13 = reg_new("d13", 13, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d14 = reg_new("d14", 14, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d15 = reg_new("d15", 15, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d16 = reg_new("d16", 16, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d17 = reg_new("d17", 17, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d18 = reg_new("d18", 18, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d19 = reg_new("d19", 19, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d20 = reg_new("d20", 20, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d21 = reg_new("d21", 21, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d22 = reg_new("d22", 22, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d23 = reg_new("d23", 23, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d24 = reg_new("d24", 24, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d25 = reg_new("d25", 25, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d26 = reg_new("d26", 26, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d27 = reg_new("d27", 27, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d28 = reg_new("d28", 28, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d29 = reg_new("d29", 29, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d30 = reg_new("d30", 30, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    d31 = reg_new("d31", 31, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);

    // 32位 浮点寄存器
    s0 = reg_new("s0", 0, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s1 = reg_new("s1", 1, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s2 = reg_new("s2", 2, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s3 = reg_new("s3", 3, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s4 = reg_new("s4", 4, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s5 = reg_new("s5", 5, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s6 = reg_new("s6", 6, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s7 = reg_new("s7", 7, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s8 = reg_new("s8", 8, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s9 = reg_new("s9", 9, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s10 = reg_new("s10", 10, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s11 = reg_new("s11", 11, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s12 = reg_new("s12", 12, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s13 = reg_new("s13", 13, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s14 = reg_new("s14", 14, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s15 = reg_new("s15", 15, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s16 = reg_new("s16", 16, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s17 = reg_new("s17", 17, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s18 = reg_new("s18", 18, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s19 = reg_new("s19", 19, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s20 = reg_new("s20", 20, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s21 = reg_new("s21", 21, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s22 = reg_new("s22", 22, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s23 = reg_new("s23", 23, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s24 = reg_new("s24", 24, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s25 = reg_new("s25", 25, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s26 = reg_new("s26", 26, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s27 = reg_new("s27", 27, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s28 = reg_new("s28", 28, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s29 = reg_new("s29", 29, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s30 = reg_new("s30", 30, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    s31 = reg_new("s31", 31, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
}
