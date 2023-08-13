#ifndef NATURE_AMD64_ABI_H
#define NATURE_AMD64_ABI_H

#include "src/lir.h"

typedef enum {
    AMD64_MODE_NULL,
    AMD64_MODE_MEMORY,
    AMD64_MODE_INTEGER,
    AMD64_MODE_SSE,
//    AMD64_MODE_X87 // 不认识，暂时忽略
} amd64_mode_t;

// 基于 operand 类型判断 arg 应该通过寄存器传递还是通过栈传递给 caller
amd64_mode_t amd64_arg_classify(closure_t *c, lir_operand_t *arg, uint *size, uint *align, uint8_t *need_reg_count);

/**
 * 返回 lir 指令列表
 * @param c
 * @param args
 * @return
 */
linked_t *amd6_lower_call(closure_t *c, lir_op_t* op);

#endif //NATURE_AMD64_ABI_H
