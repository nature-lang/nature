#include "amd64_abi.h"
#include "src/register/amd64.h"
#include "src/cross.h"

static lir_operand_t *select_return_reg(lir_operand_t *operand) {
    type_kind kind = operand_type_kind(operand);
    if (kind == TYPE_FLOAT64) {
        return operand_new(LIR_OPERAND_REG, xmm0s64);
    }
    if (kind == TYPE_FLOAT32) {
        return operand_new(LIR_OPERAND_REG, xmm0s32);
    }

    return lir_reg_operand(rax->index, kind);
}

/**
 * call test(arg1, aeg2, ...) -> result
 * @param c
 * @param op
 * @return
 */
static linked_t *amd6_lower_args(closure_t *c, slice_t *args, int64_t *stack_arg_size) {
    linked_t *result = linked_new();


    amd64_class_t lo, hi;
    uint8_t *onstack = mallocz(args->count + 1);
    lir_operand_t *rsp_operand = operand_new(LIR_OPERAND_REG, rsp);

    uint8_t sse_reg_count = 0;
    uint8_t int_reg_count = 0;

    *stack_arg_size = 0;

    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);
        *stack_arg_size += align_up(type_sizeof(arg_type), POINTER_SIZE); // 参数按照 8byte 对齐

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
    uint64_t adjust = align_up(*stack_arg_size, STACK_ALIGN_SIZE) - *stack_arg_size;
    if (adjust > 0) {
        lir_op_t *binary_op = lir_op_new(LIR_OPCODE_SUB,
                                         rsp_operand, int_operand(adjust), rsp_operand);
        linked_push(result, binary_op);
        *stack_arg_size += adjust;
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
            dst_operand = lir_reg_operand(reg_index, arg_type.kind);
        } else if (lo == AMD64_CLASS_SSE) {
            uint8_t reg_index = sse_reg_indices[sse_reg_index++];
            dst_operand = lir_reg_operand(reg_index, arg_type.kind);
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
                dst_operand = lir_reg_operand(reg_index, arg_type.kind);
            } else if (hi == AMD64_CLASS_SSE) {
                uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                dst_operand = lir_reg_operand(reg_index, arg_type.kind);
            } else {
                assert(false);
            }

            lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), arg, QWORD);
            linked_push(result, lir_op_move(dst_operand, src));
        }
    }

    return result;
}


linked_t *amd64_lower_call(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    lir_operand_t *call_result = op->output;
    slice_t *args = op->second->value;
    lir_operand_t *rsp_operand = operand_new(LIR_OPERAND_REG, rsp);

    amd64_class_t lo, hi;
    int64_t count = 0;
    if (call_result) {
        count = amd64_arg_classify(c, call_result, &lo, &hi);
        if (count == 0) {
            type_t call_result_type = lir_operand_type(call_result);
            assert(is_alloc_stack(call_result_type));
            // 需要通过内存传递，要占用一个 arg 咯
            slice_insert(args, 0, call_result);
        }
    }

    int64_t stack_arg_size = 0; // 参数 lower 占用的 stack size, 函数调用完成后需要还原
    linked_concat(result, amd6_lower_args(c, args, &stack_arg_size));

    op->second->value = slice_new();

    if (count == 2) {
        assert(call_result); // 结构体返回值(按照最高尺寸)
        lir_operand_t *lo_src;
        lir_operand_t *hi_src;
        if (lo == AMD64_CLASS_INTEGER) {
            lo_src = operand_new(LIR_OPERAND_REG, rax);
        } else {
            lo_src = operand_new(LIR_OPERAND_REG, xmm0);
        }

        if (hi == AMD64_CLASS_INTEGER) {
            hi_src = operand_new(LIR_OPERAND_REG, rdx);
        } else {
            hi_src = operand_new(LIR_OPERAND_REG, xmm1);
        }

        slice_t *output_regs = slice_new();
        slice_push(output_regs, lo_src->value);
        slice_push(output_regs, hi_src->value);
        lir_operand_t *new_output = operand_new(LIR_OPERAND_REGS, output_regs);
        linked_push(result, lir_op_new(LIR_OPCODE_CALL, op->first, op->second, new_output));

        // call_result 是一个指针地址
        lir_operand_t *lo_dst = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), call_result, 0);
        lir_operand_t *hi_dst = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), call_result, QWORD);

        linked_push(result, lir_op_move(lo_dst, lo_src));
        linked_push(result, lir_op_move(hi_dst, hi_src));
    } else if (count == 1) {
        assert(call_result); // 单返回值
        lir_operand_t *reg_operand = select_return_reg(call_result);
        linked_push(result, lir_op_new(LIR_OPCODE_CALL, op->first, op->second, reg_operand));
        // mov rax -> output
        linked_push(result, lir_op_move(call_result, reg_operand));
    } else {
        // 无返回值
        linked_push(result, op);
    }

    if (stack_arg_size > 0) {
        linked_push(result, lir_op_new(LIR_OPCODE_ADD, rsp_operand, int_operand(stack_arg_size), rsp_operand));
    }

    return result;
}


static amd64_class_t amd64_classify_merge(amd64_class_t a, amd64_class_t b) {
    if (a == b) {
        return a;
    }

    if (a == AMD64_CLASS_NO) {
        return b;
    }

    if (b == AMD64_CLASS_NO) {
        return a;
    }

    if (a == AMD64_CLASS_MEMORY || b == AMD64_CLASS_MEMORY) {
        return AMD64_CLASS_MEMORY;
    }

    if (a == AMD64_CLASS_INTEGER || b == AMD64_CLASS_INTEGER) {
        return AMD64_CLASS_INTEGER;
    }

    return AMD64_CLASS_SSE;
}

static int64_t amd64_arg_type_classify(type_t arg_type, amd64_class_t *lo, amd64_class_t *hi, uint64_t offset) {
    if (type_sizeof(arg_type) > 16) {
        return 0;
    }

    assert(offset <= 16); // 主要是 struct 会产生 offset
    if (is_float(arg_type.kind)) {
        if (offset <= 8) {
            *lo = amd64_classify_merge(*lo, AMD64_CLASS_SSE);
        } else {
            *hi = amd64_classify_merge(*hi, AMD64_CLASS_SSE);
        }
    } else if (arg_type.kind == TYPE_STRUCT) {
        // 遍历 struct 的每一个属性进行分类
        type_struct_t *type_struct = arg_type.struct_;
        offset = align_up(offset, type_struct->align); // 启示地址对齐

        for (int i = 0; i < type_struct->properties->length; i++) {
            struct_property_t *p = ct_list_value(type_struct->properties, i);
            type_t element_type = p->type;

            // 内部元素对齐
            offset = align_up(offset, type_sizeof(element_type));
            amd64_arg_type_classify(element_type, lo, hi, offset);
        }
    } else {
        if (offset <= 8) {
            *lo = amd64_classify_merge(*lo, AMD64_CLASS_INTEGER);
        } else {
            *hi = amd64_classify_merge(*hi, AMD64_CLASS_INTEGER);
        }
    }

    return *hi == AMD64_CLASS_NO ? 1 : 2;
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
    *lo = *hi = AMD64_CLASS_NO;

    // 超过 16byte 直接走 memory 传递
    if (type_sizeof(arg_type) > 16) {
        return 0;
    }

    return amd64_arg_type_classify(arg_type, lo, hi, 0);
}
