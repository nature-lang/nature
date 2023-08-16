#include "amd64_abi.h"
#include "src/register/amd64.h"
#include "src/cross.h"

linked_t *amd64_struct_mov(type_t *type, lir_operand_t *src, lir_operand_t *dst) {

}

/**
 * call test(arg1, aeg2, ...) -> result
 * @param c
 * @param op
 * @return
 */
linked_t *amd6_lower_call(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    slice_t *args = op->second->value;
    lir_operand_t *call_result = op->output;
    amd64_class_t lo, hi;

    if (call_result) {
        int64_t count = amd64_arg_classify(c, call_result, &lo, &hi);
        if (count == 0) {
            type_t call_result_type = lir_operand_type(call_result);
            assert(is_alloc_stack(call_result_type));
            // 需要通过内存传递，要占用一个 arg 咯
            slice_insert(args, 0, call_result);
        }
    }

    uint8_t *onstack = mallocz(args->count + 1);
    lir_operand_t *rsp_operand = operand_new(LIR_OPERAND_REG, rsp);

    uint8_t sse_reg_count = 0;
    uint8_t int_reg_count = 0;

    uint stack_arg_size = 0;

    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);
        stack_arg_size += align_up(type_sizeof(arg_type), POINTER_SIZE); // 参数按照 8byte 对齐

        int64_t count = amd64_arg_classify(c, arg, &lo, &hi);
        if (count == 0) { // 返回 0 就是需要内存传递
            onstack[i] = 1;
            continue;
        }

        if (lo == AMD64_CLASS_SSE && sse_reg_count <= 8) {
            onstack[i] = 0;
            sse_reg_count++;
        } else if (lo == AMD64_CLASS_INTEGER && int_reg_count <= 6) {
            onstack[i] = 0;
            int_reg_count++;
        }

        if (count == 2) {
            if (hi == AMD64_CLASS_SSE && sse_reg_count <= 8) {
                onstack[i] = 0;
                sse_reg_count++;
            } else if (hi == AMD64_CLASS_INTEGER && int_reg_count <= 6) {
                onstack[i] = 0;
                int_reg_count++;
            }
        }
    }

    // 判断是否按照 16byte 对齐
    uint64_t adjust = align_up(stack_arg_size, STACK_ALIGN_SIZE) - stack_arg_size;
    if (adjust > 0) {
        lir_op_t *binary_op = lir_op_new(LIR_OPCODE_SUB,
                                         rsp_operand, int_operand(adjust), rsp_operand);
        linked_push(result, binary_op);
    }

    // - stack 临时占用的 rsp 空间，在函数调用完成后需要归还回去
    // 优先处理需要通过 stack 传递的参数,将他们都 push 或者 mov 到栈里面(越考右的参数越优先入栈)
    for (int i = args->count - 1; i >= 0; i--) {
        lir_operand_t *arg = args->take[i];
        int64_t count = amd64_arg_classify(c, arg, &lo, &hi);

        if (onstack[i] == 0) {
            continue;
        }

        type_t arg_type = lir_operand_type(arg);
        uint16_t align_size = align_up(type_sizeof(arg_type), POINTER_SIZE);

        if (is_alloc_stack(arg_type)) {
            assert(arg->assert_type == LIR_OPERAND_VAR);

            linked_push(result, lir_op_new(LIR_OPCODE_SUB, rsp_operand, int_operand(align_size), rsp_operand));

            linked_t *temps = lir_memory_mov(c->module, arg_type, rsp_operand, arg);
            linked_concat(result, temps);
        } else {
            assert(lo == AMD64_CLASS_SSE || lo == AMD64_CLASS_INTEGER);
            lir_operand_t *dst = rsp_operand;
            linked_push(result, lir_op_new(LIR_OPCODE_SUB, dst, int_operand(align_size), dst));

            dst = indirect_addr_operand(c->module, arg_type, dst, 0);
            linked_push(result, lir_op_move(dst, arg));
        }
    }

    // - 处理通过寄存器传递的参数(选择合适的寄存器。多余的部分进行 xor 清空)
    uint8_t int_reg_indices[6] = {7, 6, 2, 1, 8, 9};
    uint8_t sse_reg_indices[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t int_reg_index = 0;
    uint8_t sse_reg_index = 0;

    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        int64_t count = amd64_arg_classify(c, arg, &lo, &hi);
        if (onstack[i] == 1) {
            continue;
        }

        type_t arg_type = lir_operand_type(arg);
        assert(count != 0);

        lir_operand_t *dst_operand;
        if (lo == AMD64_CLASS_INTEGER) {
            // 查找合适大小的 reg 进行 mov
            uint8_t reg_index = int_reg_indices[int_reg_index++];
            dst_operand = reg_operand(reg_index, arg_type.kind);
        } else if (lo == AMD64_CLASS_SSE) {
            uint8_t reg_index = sse_reg_indices[sse_reg_index++];
            dst_operand = reg_operand(reg_index, arg_type.kind);
        } else {
            assert(false);
        }

        if (is_alloc_stack(arg_type)) {
            lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), arg, 0);
            linked_push(result, lir_op_move(dst_operand, src));
        } else {
            linked_push(result, lir_op_move(dst_operand, arg));
        };

        // 将 arg 放到寄存器中, nature 中只有 struct 会超过 8byte
        if (count == 2) {
            assertf(is_alloc_stack(arg_type), "type exception cannot assign ho reg");
            if (hi == AMD64_CLASS_INTEGER) {
                uint8_t reg_index = int_reg_indices[int_reg_index++];
                dst_operand = reg_operand(reg_index, arg_type.kind);
            } else if (hi == AMD64_CLASS_SSE) {
                uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                dst_operand = reg_operand(reg_index, arg_type.kind);
            } else {
                assert(false);
            }

            lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), arg, QWORD);
            linked_push(result, lir_op_move(dst_operand, src));
        }
    }

    return result;
}

int64_t amd64_arg_type_classify(type_t arg_type, uint64_t offset) {
    // 分组，最终给一个类型出来， 遇到结构体就比较困难了。结构体嵌套结构体必须要递归
    if (arg_type.kind == TYPE_STRUCT) {
        // TODO 怎么知道当前的分组呀！管什么分组，直接计算类型？

        // 直接计算每一个属性的类型， byte offset, offset / 8 就可以得到具体拆索引，核心是什么？offset 一直传递， class 也一直传递下去？
    }
}

/**
 * 对于 struct 类型。按照 8byte 进行分组。然后对组内的每个字段进行一个分类. 最多只能有 2 个 8byte, 超过两个 8byte 则直接退出
 * @param c
 * @param arg
 * @param lo
 * @param hi
 * @return
 */
int64_t amd64_arg_classify(closure_t *c, lir_operand_t *arg, amd64_class_t *lo, amd64_class_t *hi) {
    type_t arg_type = lir_operand_type(arg);

    // struct 中包含多个 type 或者 types，分组需要做到递归平铺。一旦一个组满了就需要创建一个新的组
    // struct 每个 element 都包含一个 offset, 可以基于该 offset 进行分组


    return 0;
}
