#include "arm64.h"
#include "utils/type.h"
#include "utils/slice.h"
#include "src/cross.h"
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
reg_t *sp;
reg_t *pc;
reg_t *xzr;
reg_t *wzr;

// 浮点寄存器定义
reg_t *v0, *v1, *v2, *v3, *v4, *v5, *v6, *v7;
reg_t *v8, *v9, *v10, *v11, *v12, *v13, *v14, *v15;
reg_t *v16, *v17, *v18, *v19, *v20, *v21, *v22, *v23;
reg_t *v24, *v25, *v26, *v27, *v28, *v29, *v30, *v31;

void arm64_reg_init() {
    // 通用寄存器定义
    x0 = reg_new("x0", 0, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x1 = reg_new("x1", 1, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x2 = reg_new("x2", 2, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x3 = reg_new("x3", 3, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x4 = reg_new("x4", 4, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x5 = reg_new("x5", 5, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x6 = reg_new("x6", 6, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x7 = reg_new("x7", 7, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x8 = reg_new("x8", 8, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x9 = reg_new("x9", 9, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x10 = reg_new("x10", 10, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x11 = reg_new("x11", 11, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x12 = reg_new("x12", 12, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x13 = reg_new("x13", 13, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x14 = reg_new("x14", 14, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x15 = reg_new("x15", 15, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x16 = reg_new("x16", 16, 0, QWORD, 0);  // IP0, 不参与分配
    x17 = reg_new("x17", 17, 0, QWORD, 0);  // IP1, 不参与分配
    x18 = reg_new("x18", 18, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x19 = reg_new("x19", 19, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x20 = reg_new("x20", 20, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x21 = reg_new("x21", 21, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x22 = reg_new("x22", 22, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x23 = reg_new("x23", 23, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x24 = reg_new("x24", 24, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x25 = reg_new("x25", 25, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x26 = reg_new("x26", 26, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x27 = reg_new("x27", 27, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x28 = reg_new("x28", 28, LIR_FLAG_ALLOC_INT, QWORD, 0);
    x29 = reg_new("x29", 29, 0, QWORD, 0);  // FP, 不参与分配
    x30 = reg_new("x30", 30, 0, QWORD, 0);  // LR, 不参与分配

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
    w16 = reg_new("w16", 16, 0, DWORD, 0);  // IP0, 不参与分配
    w17 = reg_new("w17", 17, 0, DWORD, 0);  // IP1, 不参与分配
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
    w29 = reg_new("w29", 29, 0, DWORD, 0);  // FP, 不参与分配
    w30 = reg_new("w30", 30, 0, DWORD, 0);  // LR, 不参与分配

    // 初始化特殊寄存器
    sp = reg_new("sp", 31, LIR_FLAG_ALLOC_INT, QWORD, 0);
    pc = reg_new("pc", 32, 0, QWORD, 0);
    xzr = reg_new("xzr", 31, 0, QWORD, 0);
    wzr = reg_new("wzr", 31, 0, DWORD, 0);

    // 浮点寄存器
    v0 = reg_new("v0", 0, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v1 = reg_new("v1", 1, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v2 = reg_new("v2", 2, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v3 = reg_new("v3", 3, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v4 = reg_new("v4", 4, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v5 = reg_new("v5", 5, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v6 = reg_new("v6", 6, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v7 = reg_new("v7", 7, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v8 = reg_new("v8", 8, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v9 = reg_new("v9", 9, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v10 = reg_new("v10", 10, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v11 = reg_new("v11", 11, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v12 = reg_new("v12", 12, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v13 = reg_new("v13", 13, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v14 = reg_new("v14", 14, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v15 = reg_new("v15", 15, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v16 = reg_new("v16", 16, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v17 = reg_new("v17", 17, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v18 = reg_new("v18", 18, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v19 = reg_new("v19", 19, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v20 = reg_new("v20", 20, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v21 = reg_new("v21", 21, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v22 = reg_new("v22", 22, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v23 = reg_new("v23", 23, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v24 = reg_new("v24", 24, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v25 = reg_new("v25", 25, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v26 = reg_new("v26", 26, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v27 = reg_new("v27", 27, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v28 = reg_new("v28", 28, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v29 = reg_new("v29", 29, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v30 = reg_new("v30", 30, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
    v31 = reg_new("v31", 31, LIR_FLAG_ALLOC_FLOAT, QWORD, 0);
}

alloc_kind_e arm64_alloc_kind_of_def(closure_t *c, lir_op_t *op, lir_var_t *var) {
    if (lir_op_contain_cmp(op)) {
        if (var->flag & FLAG(LIR_FLAG_OUTPUT)) {
            return ALLOC_KIND_SHOULD;
        }
    }
    if (op->code == LIR_OPCODE_MOVE) {
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

    if (op->code == LIR_OPCODE_NOP) {
        return ALLOC_KIND_SHOULD;
    }

    return ALLOC_KIND_MUST;
}

alloc_kind_e arm64_alloc_kind_of_use(closure_t *c, lir_op_t *op, lir_var_t *var) {
    if (var->flag & FLAG(LIR_FLAG_INDIRECT_ADDR_BASE)) {
        return ALLOC_KIND_MUST;
    }

    if (op->code == LIR_OPCODE_LEA) {
        return ALLOC_KIND_NOT;
    }

    if (op->code == LIR_OPCODE_MOVE) {
        if (op->output->assert_type == LIR_OPERAND_SYMBOL_VAR ||
            op->output->assert_type == LIR_OPERAND_STACK ||
            op->output->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
            return ALLOC_KIND_MUST;
        }
    }

    if (lir_op_term(op)) {
        assertf(op->first->assert_type == LIR_OPERAND_VAR, "arithmetic op first operand must var for assign reg");
        if (var->flag & FLAG(LIR_FLAG_FIRST)) {
            return ALLOC_KIND_MUST;
        }
    }

    return ALLOC_KIND_SHOULD;
}