#include "amd64.h"
#include "utils/type.h"
#include "utils/slice.h"
#include "src/cross.h"
#include <stdio.h>


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
    sp = reg_new("sp", 4, LIR_FLAG_ALLOC_INT, WORD, 0);
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

reg_t *amd64_reg_select(uint8_t index, type_kind kind) {
    uint8_t select_size = type_kind_sizeof(kind);
    uint8_t alloc_type = type_base_trans_alloc(kind);
    if (alloc_type == LIR_FLAG_ALLOC_FLOAT) {
        select_size = OWORD; // 固定使用 xmm0 ~ xmm15
    }

    return reg_find(index, select_size);
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
reg_t *amd64_fn_param_next_reg(uint8_t *used, type_kind base) {
    uint8_t reg_type = type_base_trans_alloc(base);
    uint8_t used_index = 0;
    if (reg_type == LIR_FLAG_ALLOC_FLOAT) {
        used_index = 1;
    }
    uint8_t index = used[used_index]++;
    uint8_t int_param_indexes[] = {7, 6, 2, 1, 8, 9};
    // 通用寄存器 (0~5 = 6 个) rdi, rsi, rdx, rcx, r8, r9
    if (reg_type == LIR_FLAG_ALLOC_INT && index <= 5) {
        uint8_t reg_index = int_param_indexes[index];
        return (reg_t *) cross_reg_select(reg_index, base);
    }

    // 浮点寄存器(0~7 = 8 个) xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7
    if (reg_type == LIR_FLAG_ALLOC_FLOAT && index <= 7) {
        return (reg_t *) cross_reg_select(index, base);
    }

    return NULL;
}


/**
 * output 没有特殊情况就必须分一个寄存器，主要是 amd64 的指令基本都是需要寄存器参与的
 * @param c
 * @param op
 * @param i
 * @return
 */
alloc_kind_e amd64_alloc_kind_of_def(closure_t *c, lir_op_t *op, lir_var_t *var) {
    if (lir_op_contain_cmp(op)) {
        if (var->flag & FLAG(LIR_FLAG_OUTPUT)) { // 顶层 var 才不用分配寄存器，否则 var 可能只是 indirect_addr base
            return ALLOC_KIND_SHOULD;
        }
    }
    if (op->code == LIR_OPCODE_MOVE) {
        // 如果 left == imm 或者 reg 则返回 should, mov 的 first 一定是 use
        assertf(var->flag & FLAG(LIR_FLAG_OUTPUT), "move def must in output");

        lir_operand_t *first = op->first;
        if (first->assert_type == LIR_OPERAND_REG) {
            return ALLOC_KIND_SHOULD;
        }

        if (first->assert_type == LIR_OPERAND_IMM) {
            lir_imm_t *imm = first->value;
            if (!is_qword_int(imm->kind)) {
                return ALLOC_KIND_SHOULD;
            }
        }
    }
    if (op->code == LIR_OPCODE_CLV) {
        return ALLOC_KIND_SHOULD;
    }

    // phi 也属于 def, 所以必须分配寄存器
    return ALLOC_KIND_MUST;
}

/**
 * 如果 op type is var, 且是 indirect addr, 则必须分配一个寄存器，用于地址 indirect addr
 * output 已经必须要有寄存器了, input 就无所谓了
 * @param c
 * @param op
 * @param i
 * @return
 */
alloc_kind_e amd64_alloc_kind_of_use(closure_t *c, lir_op_t *op, lir_var_t *var) {
    // lea 指令的 use 一定是 first, 所以不需要重复判断
    if (op->code == LIR_OPCODE_LEA) {
        return ALLOC_KIND_NOT;
    }

    if (op->code == LIR_OPCODE_MOVE) {
        if (op->output->assert_type == LIR_OPERAND_SYMBOL_VAR) {
            return ALLOC_KIND_MUST;
        }
    }

    if (lir_op_term(op)) {
        assertf(op->first->assert_type == LIR_OPERAND_VAR, "arithmetic op first operand must var for assign reg");
        if (var->flag & FLAG(LIR_FLAG_FIRST)) {
            return ALLOC_KIND_MUST;
        }
    }

    // 比较运算符实用了 op cmp, 所以 cmp 的 first 或者 second 其中一个必须是寄存器
    // 如果优先分配给 first, 如果 first 不是寄存器，则分配给 second
    if (lir_op_contain_cmp(op)) { // cmp indirect addr
        assert((op->first->assert_type == LIR_OPERAND_VAR || op->second->assert_type == LIR_OPERAND_VAR) &&
               "cmp must have var, var can allocate registers");

        if (var->flag & FLAG(LIR_FLAG_FIRST)) {
            return ALLOC_KIND_MUST;
        }

        // second 只能是在 first 非 var 的情况下才能分配寄存器
        if (var->flag & FLAG(LIR_FLAG_SECOND) && op->first->assert_type != LIR_OPERAND_VAR) {
            // 优先将寄存器分配给 first, 仅当 first 不是 var 时才分配给 second
            return ALLOC_KIND_MUST;
        }
    }

    // var 是 indirect addr 的 base 部分， native indirect addr 则必须借助寄存器
    if (var->flag & FLAG(LIR_FLAG_INDIRECT_ADDR_BASE)) {
        return ALLOC_KIND_MUST;
    }

    return ALLOC_KIND_SHOULD;
}

