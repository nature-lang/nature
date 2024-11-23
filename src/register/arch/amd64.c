#include "amd64.h"
#include "src/register/register.h"
#include <stdio.h>

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
//reg_t *sp;
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

void amd64_reg_init() {
    rax = reg_new("rax", 0, LIR_FLAG_ALLOC_INT, QWORD, 1);
    rcx = reg_new("rcx", 1, LIR_FLAG_ALLOC_INT, QWORD, 2);
    rdx = reg_new("rdx", 2, LIR_FLAG_ALLOC_INT, QWORD, 3);
    rbx = reg_new("rbx", 3, LIR_FLAG_ALLOC_INT, QWORD, 0);
    rsp = reg_new("rsp", 4, LIR_FLAG_ALLOC_INT, QWORD, 0);
    rbp = reg_new("rbp", 5, LIR_FLAG_ALLOC_INT, QWORD, 0);
    rsi = reg_new("rsi", 6, LIR_FLAG_ALLOC_INT, QWORD, 4);
    rdi = reg_new("rdi", 7, LIR_FLAG_ALLOC_INT, QWORD, 5);
    r8 = reg_new("r8", 8, LIR_FLAG_ALLOC_INT, QWORD, 6);
    r9 = reg_new("r9", 9, LIR_FLAG_ALLOC_INT, QWORD, 7);
    r10 = reg_new("r10", 10, LIR_FLAG_ALLOC_INT, QWORD, 8);
    r11 = reg_new("r11", 11, LIR_FLAG_ALLOC_INT, QWORD, 9);
    r12 = reg_new("r12", 12, LIR_FLAG_ALLOC_INT, QWORD, 10);
    r13 = reg_new("r13", 13, LIR_FLAG_ALLOC_INT, QWORD, 11);
    r14 = reg_new("r14", 14, LIR_FLAG_ALLOC_INT, QWORD, 12);
    r15 = reg_new("r15", 15, LIR_FLAG_ALLOC_INT, QWORD, 13);

    eax = reg_new("eax", 0, LIR_FLAG_ALLOC_INT, DWORD, 0);
    ecx = reg_new("ecx", 1, LIR_FLAG_ALLOC_INT, DWORD, 0);
    edx = reg_new("edx", 2, LIR_FLAG_ALLOC_INT, DWORD, 0);
    ebx = reg_new("ebx", 3, LIR_FLAG_ALLOC_INT, DWORD, 0);
    esp = reg_new("esp", 4, LIR_FLAG_ALLOC_INT, DWORD, 0);
    ebp = reg_new("ebp", 5, LIR_FLAG_ALLOC_INT, DWORD, 0);
    esi = reg_new("esi", 6, LIR_FLAG_ALLOC_INT, DWORD, 0);
    edi = reg_new("edi", 7, LIR_FLAG_ALLOC_INT, DWORD, 0);
    r8d = reg_new("r8d", 8, LIR_FLAG_ALLOC_INT, DWORD, 0);
    r9d = reg_new("r9d", 9, LIR_FLAG_ALLOC_INT, DWORD, 0);
    r10d = reg_new("r10d", 10, LIR_FLAG_ALLOC_INT, DWORD, 0);
    r11d = reg_new("r11d", 11, LIR_FLAG_ALLOC_INT, DWORD, 0);
    r12d = reg_new("r12d", 12, LIR_FLAG_ALLOC_INT, DWORD, 0);
    r13d = reg_new("r13d", 13, LIR_FLAG_ALLOC_INT, DWORD, 0);
    r14d = reg_new("r14d", 14, LIR_FLAG_ALLOC_INT, DWORD, 0);
    r15d = reg_new("r15d", 15, LIR_FLAG_ALLOC_INT, DWORD, 0);

    ax = reg_new("ax", 0, LIR_FLAG_ALLOC_INT, WORD, 0);
    cx = reg_new("cx", 1, LIR_FLAG_ALLOC_INT, WORD, 0);
    dx = reg_new("dx", 2, LIR_FLAG_ALLOC_INT, WORD, 0);
    bx = reg_new("bx", 3, LIR_FLAG_ALLOC_INT, WORD, 0);
    reg_new("sp", 4, LIR_FLAG_ALLOC_INT, WORD, 0);
    bp = reg_new("bp", 5, LIR_FLAG_ALLOC_INT, WORD, 0);
    si = reg_new("si", 6, LIR_FLAG_ALLOC_INT, WORD, 0);
    di = reg_new("di", 7, LIR_FLAG_ALLOC_INT, WORD, 0);
    r8w = reg_new("r8w", 8, LIR_FLAG_ALLOC_INT, WORD, 0);
    r9w = reg_new("r9w", 9, LIR_FLAG_ALLOC_INT, WORD, 0);
    r10w = reg_new("r10w", 10, LIR_FLAG_ALLOC_INT, WORD, 0);
    r11w = reg_new("r11w", 11, LIR_FLAG_ALLOC_INT, WORD, 0);
    r12w = reg_new("r12w", 12, LIR_FLAG_ALLOC_INT, WORD, 0);
    r13w = reg_new("r13w", 13, LIR_FLAG_ALLOC_INT, WORD, 0);
    r14w = reg_new("r14w", 14, LIR_FLAG_ALLOC_INT, WORD, 0);
    r15w = reg_new("r15w", 15, LIR_FLAG_ALLOC_INT, WORD, 0);

    // 正常来说也应该是 0, 1, 2, 3, 但是 intel 手册规定了使用 4，5，6, 7
    ah = reg_new("ah", 4, LIR_FLAG_ALLOC_INT, BYTE, 0);
    ch = reg_new("ch", 5, LIR_FLAG_ALLOC_INT, BYTE, 0);
    dh = reg_new("dh", 6, LIR_FLAG_ALLOC_INT, BYTE, 0);
    bh = reg_new("bh", 7, LIR_FLAG_ALLOC_INT, BYTE, 0);

    // al 是低 8 字节，ah 是高 8 字节
    al = reg_new("al", 0, LIR_FLAG_ALLOC_INT, BYTE, 0);
    cl = reg_new("cl", 1, LIR_FLAG_ALLOC_INT, BYTE, 0);
    dl = reg_new("dl", 2, LIR_FLAG_ALLOC_INT, BYTE, 0);
    bl = reg_new("bl", 3, LIR_FLAG_ALLOC_INT, BYTE, 0);
    spl = reg_new("spl", 4, LIR_FLAG_ALLOC_INT, BYTE, 0);
    bpl = reg_new("bpl", 5, LIR_FLAG_ALLOC_INT, BYTE, 0);
    sil = reg_new("sil", 6, LIR_FLAG_ALLOC_INT, BYTE, 0);
    dil = reg_new("dil", 7, LIR_FLAG_ALLOC_INT, BYTE, 0);
    r8b = reg_new("r8b", 8, LIR_FLAG_ALLOC_INT, BYTE, 0);
    r9b = reg_new("r9b", 9, LIR_FLAG_ALLOC_INT, BYTE, 0);
    r10b = reg_new("r10b", 10, LIR_FLAG_ALLOC_INT, BYTE, 0);
    r11b = reg_new("r11b", 11, LIR_FLAG_ALLOC_INT, BYTE, 0);
    r12b = reg_new("r12b", 12, LIR_FLAG_ALLOC_INT, BYTE, 0);
    r13b = reg_new("r13b", 13, LIR_FLAG_ALLOC_INT, BYTE, 0);
    r14b = reg_new("r14b", 14, LIR_FLAG_ALLOC_INT, BYTE, 0);
    r15b = reg_new("r15b", 15, LIR_FLAG_ALLOC_INT, BYTE, 0);

    xmm0 = reg_new("xmm0", 0, LIR_FLAG_ALLOC_FLOAT, OWORD, 14);
    xmm1 = reg_new("xmm1", 1, LIR_FLAG_ALLOC_FLOAT, OWORD, 15);
    xmm2 = reg_new("xmm2", 2, LIR_FLAG_ALLOC_FLOAT, OWORD, 16);
    xmm3 = reg_new("xmm3", 3, LIR_FLAG_ALLOC_FLOAT, OWORD, 17);
    xmm4 = reg_new("xmm4", 4, LIR_FLAG_ALLOC_FLOAT, OWORD, 18);
    xmm5 = reg_new("xmm5", 5, LIR_FLAG_ALLOC_FLOAT, OWORD, 19);
    xmm6 = reg_new("xmm6", 6, LIR_FLAG_ALLOC_FLOAT, OWORD, 20);
    xmm7 = reg_new("xmm7", 7, LIR_FLAG_ALLOC_FLOAT, OWORD, 21);
    xmm8 = reg_new("xmm8", 8, LIR_FLAG_ALLOC_FLOAT, OWORD, 22);
    xmm9 = reg_new("xmm9", 9, LIR_FLAG_ALLOC_FLOAT, OWORD, 23);
    xmm10 = reg_new("xmm10", 10, LIR_FLAG_ALLOC_FLOAT, OWORD, 24);
    xmm11 = reg_new("xmm11", 11, LIR_FLAG_ALLOC_FLOAT, OWORD, 25);
    xmm12 = reg_new("xmm12", 12, LIR_FLAG_ALLOC_FLOAT, OWORD, 26);
    xmm13 = reg_new("xmm13", 13, LIR_FLAG_ALLOC_FLOAT, OWORD, 27);
    xmm14 = reg_new("xmm14", 14, LIR_FLAG_ALLOC_FLOAT, OWORD, 28);
    xmm15 = reg_new("xmm15", 15, LIR_FLAG_ALLOC_FLOAT, OWORD, 29);

    xmm0s32 = reg_new("xmm0s32", 0, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm1s32 = reg_new("xmm1s32", 1, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm2s32 = reg_new("xmm2s32", 2, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm3s32 = reg_new("xmm3s32", 3, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm4s32 = reg_new("xmm4s32", 4, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm5s32 = reg_new("xmm5s32", 5, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm6s32 = reg_new("xmm6s32", 6, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm7s32 = reg_new("xmm7s32", 7, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm8s32 = reg_new("xmm8s32", 8, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm9s32 = reg_new("xmm9s32", 9, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm10s32 = reg_new("xmm10s32", 10, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm11s32 = reg_new("xmm11s32", 11, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm12s32 = reg_new("xmm12s32", 12, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm13s32 = reg_new("xmm13s32", 13, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm14s32 = reg_new("xmm14s32", 14, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);
    xmm15s32 = reg_new("xmm15s32", 15, LIR_FLAG_ALLOC_FLOAT, DWORD, 0);

    xmm0s64 = reg_new("xmm0s64", 0, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm1s64 = reg_new("xmm1s64", 1, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm2s64 = reg_new("xmm2s64", 2, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm3s64 = reg_new("xmm3s64", 3, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm4s64 = reg_new("xmm4s64", 4, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm5s64 = reg_new("xmm5s64", 5, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm6s64 = reg_new("xmm6s64", 6, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm7s64 = reg_new("xmm7s64", 7, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm8s64 = reg_new("xmm8s64", 8, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm9s64 = reg_new("xmm9s64", 9, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm10s64 = reg_new("xmm10s64", 10, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm11s64 = reg_new("xmm11s64", 11, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm12s64 = reg_new("xmm12s64", 12, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm13s64 = reg_new("xmm13s64", 13, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm14s64 = reg_new("xmm14s64", 14, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    xmm15s64 = reg_new("xmm15s64", 15, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);


    ymm0 = reg_new("ymm0", 0, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm1 = reg_new("ymm1", 1, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm2 = reg_new("ymm2", 2, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm3 = reg_new("ymm3", 3, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm4 = reg_new("ymm4", 4, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm5 = reg_new("ymm5", 5, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm6 = reg_new("ymm6", 6, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm7 = reg_new("ymm7", 7, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm8 = reg_new("ymm8", 8, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm9 = reg_new("ymm9", 9, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm10 = reg_new("ymm10", 10, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm11 = reg_new("ymm11", 11, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm12 = reg_new("ymm12", 12, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm13 = reg_new("ymm13", 13, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm14 = reg_new("ymm14", 14, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);
    ymm15 = reg_new("ymm15", 15, LIR_FLAG_ALLOC_FLOAT, YWORD, 0);

    zmm0 = reg_new("zmm0", 0, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm1 = reg_new("zmm1", 1, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm2 = reg_new("zmm2", 2, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm3 = reg_new("zmm3", 3, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm4 = reg_new("zmm4", 4, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm5 = reg_new("zmm5", 5, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm6 = reg_new("zmm6", 6, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm7 = reg_new("zmm7", 7, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm8 = reg_new("zmm8", 8, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm9 = reg_new("zmm9", 9, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm10 = reg_new("zmm10", 10, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm11 = reg_new("zmm11", 11, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm12 = reg_new("zmm12", 12, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm13 = reg_new("zmm13", 13, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm14 = reg_new("zmm14", 14, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
    zmm15 = reg_new("zmm15", 15, LIR_FLAG_ALLOC_FLOAT, ZWORD, 0);
}

/**
 * TODO 选择 fit reg 还是大 reg?
 * amd64 下统一使用 8byte 寄存器或者 16byte xmm 寄存器
 * 返回下一个可用的寄存器或者内存地址
 * 但是这样就需要占用 rbp 偏移，怎么做？
 * 每个函数定义的最开始已经使用 sub n => rsp, 已经申请了栈空间 [0 ~ n]
 * 后续函数中调用其他函数所使用的栈空间都是在 [n ~ 无限] 中, 直接通过 push 操作即可
 * 但是需要注意 push 的顺序，最后的参数先 push (也就是指令反向 merge)
 * 如果返回了 NULL 就说明没有可用的寄存器啦，加一条 push 就行了
 * @param count
 * @param size
 * @return
 */
//reg_t *amd64_fn_param_next_reg(uint8_t *used, type_kind kind) {
//    bool floated = is_float(kind);
//    uint8_t used_index = 0;
//    if (floated) {
//        used_index = 1;
//    }
//    uint8_t index = used[used_index]++;
//    uint8_t int_param_indexes[] = {7, 6, 2, 1, 8, 9};
//    // 通用寄存器 (0~5 = 6 个) rdi, rsi, rdx, rcx, r8, r9
//    if (!floated && index <= 5) {
//        uint8_t reg_index = int_param_indexes[index];
//        return (reg_t *) cross_reg_select(reg_index, kind);
//    }
//
//    // 浮点寄存器(0~7 = 8 个) xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7
//    if (floated && index <= 7) {
//        return (reg_t *) cross_reg_select(index, kind);
//    }
//
//    return NULL;
//}

