#ifndef NATURE_AMD64_ABI_H
#define NATURE_AMD64_ABI_H

#include "src/lir.h"

typedef enum {
    AMD64_CLASS_NO,
    AMD64_CLASS_MEMORY,
    AMD64_CLASS_INTEGER,
    AMD64_CLASS_SSE,
//    AMD64_MODE_X87 // 不认识，暂时忽略
} amd64_class_t;

// 基于 operand 类型判断 arg 应该通过寄存器传递还是通过栈传递给 caller
int64_t amd64_arg_classify(closure_t *c, lir_operand_t *arg, amd64_class_t *lo, amd64_class_t *hi);

/**
 * 返回 lir 指令列表
 * @param c
 * @param args
 * @return
 */
linked_t *amd6_lower_call(closure_t *c, lir_op_t *op);

#endif //NATURE_AMD64_ABI_H
