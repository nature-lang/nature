#include "amd64_abi.h"

/**
 * call test(arg1, aeg2, ...) -> result
 * @param c
 * @param op
 * @return
 */
linked_t *amd6_lower_call(closure_t *c, lir_op_t *op) {
    slice_t *args = op->second->value;
    lir_operand_t *res = op->output;

    uint8_t *onstack = mallocz(args->count + 1);

    uint8_t sse_reg_count = 0;
    uint8_t int_reg_count = 0;

    uint8_t need_reg_count = 0;
    uint size = 0;
    uint align = 0; // 尤其是当参数是结构体时，需要能够知道结构体的对齐规则

    // - 确定返回值的类型，如果其是 memory, 则需要将其添加到 args 的头部(通过内存地址)
    for (int i = 0; i < args->count; ++i) {
        amd64_mode_t mode = amd64_arg_classify(c, args->take[i], &size, &align, &need_reg_count);
        if (mode == AMD64_MODE_SSE && (sse_reg_count + need_reg_count) <= 8) {
            onstack[i] = false;
            sse_reg_count += need_reg_count;
        } else if (mode == AMD64_MODE_INTEGER && (int_reg_count + need_reg_count) <= 6) {
            onstack[i] = false;
            int_reg_count += need_reg_count;
        } else if (mode == AMD64_MODE_NULL) {
            onstack[i] = false;  // TODO 什么情况下会返回 amd64_mode_null
        } else {
            // 参数 size 过大，或者是参数数量过多，此时需要将 push 到栈里面
        }
    }


    // - 遍历参数，确定参数是通过寄存器还是 stack 传递


    // - 处理需要通过 stack 传递的参数，进行入栈处理


    linked_t *result;
    return result;
}
