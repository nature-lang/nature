#include "amd64_abi.h"
#include "src/register/amd64.h"
#include "src/cross.h"

lir_operand_t *select_return_reg(lir_operand_t *operand) {
    type_kind kind = operand_type_kind(operand);
    if (kind == TYPE_FLOAT64) {
        return operand_new(LIR_OPERAND_REG, xmm0s64);
    }
    if (kind == TYPE_FLOAT32) {
        return operand_new(LIR_OPERAND_REG, xmm0s32);
    }

    return lir_reg_operand(rax->index, kind);
}

linked_t *amd64_lower_fn_end(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    if (!c->return_operand) {
        goto END;
    }

    lir_operand_t *return_operand = op->first;
    type_t t = lir_operand_type(return_operand);
    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    int64_t count = amd64_type_classify(t, &lo, &hi, 0);

    if (count == 0) {
        // 参数已经通过内存给到了目标, 按照规约，还需要将 return_operand 中的值 move 到 rax 中
        linked_push(result, lir_op_move(operand_new(LIR_OPERAND_REG, rax), return_operand));
        goto END;
    }

    lir_operand_t *lo_dst_reg = NULL;
    type_kind lo_kind;
    if (lo == AMD64_CLASS_INTEGER) {
        lo_dst_reg = operand_new(LIR_OPERAND_REG, rax);
        lo_kind = TYPE_UINT64;
    } else {
        lo_dst_reg = operand_new(LIR_OPERAND_REG, xmm0s64);
        lo_kind = TYPE_FLOAT64;
    }

    if (t.kind == TYPE_STRUCT) {
        // return_operand 保存的是一个指针，需要 indirect 这个值，将响应的值 mov 到寄存器中
        lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(lo_kind), return_operand, 0);
        linked_push(result, lir_op_move(lo_dst_reg, src));

        if (count == 2) {
            lir_operand_t *hi_dst_reg = NULL;
            type_kind hi_kind;
            if (hi == AMD64_CLASS_INTEGER) {
                hi_dst_reg = operand_new(LIR_OPERAND_REG, rdx);
                hi_kind = TYPE_UINT64;
            } else {
                hi_dst_reg = operand_new(LIR_OPERAND_REG, xmm1s64);
                hi_kind = TYPE_FLOAT64;
            }
            src = indirect_addr_operand(c->module, type_kind_new(hi_kind), return_operand, QWORD);
            linked_push(result, lir_op_move(hi_dst_reg, src));
        }
    } else {
        assert(count == 1);
        assertf(t.kind != TYPE_ARRAY, "array type must be pointer type");

        // 由于需要直接 mov， 所以还是需要选择合适的大小
        reg_t *lo_reg = lo_dst_reg->value;
        lo_dst_reg = operand_new(LIR_OPERAND_REG, amd64_reg_select(lo_reg->index, t.kind));

        linked_push(result, lir_op_move(lo_dst_reg, return_operand));
    }

    END:
    op->first = NULL;
    linked_push(result, op);

    return result;
}

/**
 * 参照 linux abi 规范 将参数从寄存器或者栈中取出来，存放到 formal vars 中
 * @param c
 * @param formals
 * @return
 */
static linked_t *amd64_lower_params(closure_t *c, slice_t *param_vars) {
    linked_t *result = linked_new();

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    uint8_t *onstack = mallocz(param_vars->count + 1);

    int64_t stack_param_slot = 16; // by rbp, rest addr 和 push rsp 占用了 16 个字节

    uint8_t sse_reg_count = 0;
    uint8_t int_reg_count = 0;

    for (int i = 0; i < param_vars->count; ++i) {
        lir_var_t *var = param_vars->take[i];
        type_t param_type = var->type;

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(param_type, &lo, &hi, 0);
        if (count == 0) {
            onstack[i] = 1;
            continue;
        } else if (count == 1) {
            if (lo == AMD64_CLASS_SSE && sse_reg_count + 1 <= 8) {
                onstack[i] = 0;
                sse_reg_count++;
            } else if (lo == AMD64_CLASS_INTEGER && int_reg_count + 1 <= 6) {
                onstack[i] = 0;
                int_reg_count++;
            } else {
                onstack[i] = 1;
                continue;
            }
        } else if (count == 2) {
            if (lo == hi) {
                if (lo == AMD64_CLASS_SSE && sse_reg_count + 2 <= 8) {
                    onstack[i] = 0;
                    sse_reg_count += 2;
                } else if (lo == AMD64_CLASS_INTEGER && int_reg_count + 2 <= 6) {
                    onstack[i] = 0;
                    int_reg_count += 2;
                } else {
                    onstack[i] = 1;
                }
            } else {
                if (sse_reg_count + 1 <= 8 && int_reg_count + 1 <= 6) {
                    onstack[i] = 0;
                    sse_reg_count++;
                    int_reg_count++;
                } else {
                    onstack[i] = 1;
                }
            }
        }
    }

    uint8_t int_reg_indices[6] = {7, 6, 2, 1, 8, 9};
    uint8_t sse_reg_indices[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t int_reg_index = 0;
    uint8_t sse_reg_index = 0;

    // - stack 临时占用的 rsp 空间，在函数调用完成后需要归还回去
    // 优先处理需要通过 stack 传递的参数,将他们都 push 或者 mov 到栈里面(越考右的参数越优先入栈)
    for (int i = 0; i < param_vars->count; ++i) {
        lir_var_t *var = param_vars->take[i];
        type_t param_type = var->type;

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(param_type, &lo, &hi, 0);
        lir_operand_t *dst_param = operand_new(LIR_OPERAND_VAR, var);

        if (onstack[i]) {
            // 最右侧参数先入站，所以最右侧参数在栈顶，合理的方式是从左往右遍历读取 source 了。
            // 结构体由于 caller 已经申请了 stack 空间，所以参数中直接使用该空间的地址即可
            // 而不需要进行重复的 copy 和 stack 空间申请
            lir_operand_t *src = lir_stack_operand(c->module, stack_param_slot, type_sizeof(param_type));

            // stack 会被 native 成 indirect_addr [rbp+slot], 如果其中保存的是结构体，这里需要的是地址。
            if (is_alloc_stack(param_type)) {
                lir_operand_t *src_ref = lower_temp_var_operand(c, result, type_kind_new(TYPE_CPTR));
                linked_push(result, lir_op_lea(src_ref, src));
                linked_push(result, lir_op_move(dst_param, src_ref));
            } else {
                linked_push(result, lir_op_move(dst_param, src));
            }

            // 最后一个参数是 fn_runtime_operand 参数，将其记录下来
            if (c->fn_runtime_operand != NULL && i == param_vars->count - 1) {
                c->fn_runtime_stack = stack_param_slot;  // 最后一个参数所在的栈的起点
            }

            stack_param_slot += align_up(type_sizeof(param_type), QWORD); // 参数按照 8byte 对齐
        } else {
            if (param_type.kind == TYPE_STRUCT) {
                lir_stack_alloc(c, result, param_type, dst_param);

                // 从寄存器中将数据移入到低保空间
                lir_operand_t *lo_reg_operand;
                type_kind lo_kind;
                if (lo == AMD64_CLASS_INTEGER) {
                    // 查找合适大小的 reg 进行 mov
                    uint8_t reg_index = int_reg_indices[int_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, TYPE_UINT64));
                    lo_kind = TYPE_UINT64;
                } else if (lo == AMD64_CLASS_SSE) {
                    uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, TYPE_FLOAT64));
                    lo_kind = TYPE_FLOAT64;
                } else {
                    assert(false);
                }

                lir_operand_t *lo_dst = indirect_addr_operand(c->module,
                                                              type_kind_new(lo_kind), dst_param, 0);

                linked_push(result, lir_op_move(lo_dst, lo_reg_operand));

                if (count == 2) {
                    // 从寄存器中将数据移入到低保空间
                    lir_operand_t *hi_reg_operand;
                    type_kind hi_kind;
                    if (hi == AMD64_CLASS_INTEGER) {
                        // 查找合适大小的 reg 进行 mov
                        uint8_t reg_index = int_reg_indices[int_reg_index++];
                        hi_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, TYPE_UINT64));
                        hi_kind = TYPE_UINT64;
                    } else if (hi == AMD64_CLASS_SSE) {
                        uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                        hi_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, TYPE_FLOAT64));
                        hi_kind = TYPE_FLOAT64;
                    } else {
                        assert(false);
                    }

                    lir_operand_t *hi_dst = indirect_addr_operand(c->module,
                                                                  type_kind_new(lo_kind), dst_param, QWORD);

                    linked_push(result, lir_op_move(hi_dst, hi_reg_operand));
                }
            } else {
                assertf(param_type.kind != TYPE_ARRAY, "array type must be pointer type");
                assertf(count == 1, "the normal type uses only one register");
                // 从寄存器中将数据移入到低保空间
                lir_operand_t *lo_reg_operand;
                uint8_t reg_index;
                if (lo == AMD64_CLASS_INTEGER) {
                    // 查找合适大小的 reg 进行 mov
                    reg_index = int_reg_indices[int_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, param_type.kind));
                } else if (lo == AMD64_CLASS_SSE) {
                    reg_index = sse_reg_indices[sse_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, param_type.kind));
                } else {
                    assert(false);
                }

                // fn runtime operand 就是一个 type_fn 类型的指针
                if (c->fn_runtime_operand != NULL && i == param_vars->count - 1) {
                    c->fn_runtime_reg = reg_index;
                }


                linked_push(result, lir_op_move(dst_param, lo_reg_operand));
            }
        }
    }

    return result;
}

linked_t *amd64_lower_fn_begin(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    linked_push(result, op);

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    slice_t *params = op->output->value;

    if (c->return_operand) {
        type_t return_type = lir_operand_type(c->return_operand);
        int64_t count = amd64_type_classify(return_type, &lo, &hi, 0);

        if (count == 0) {
            // rsi 中保存了地址指针，现在需要走普通 mov 做一次 mov 处理就行了
            lir_operand_t *return_operand = c->return_operand;
            assert(return_operand->assert_type == LIR_OPERAND_VAR);
            return_operand = lir_operand_copy(return_operand);
            lir_var_t *var = return_operand->value;
            assertf(is_alloc_stack(var->type), "only struct can use stack pass");
            // 这里必须使用 ptr, 如果使用 struct，lower params 就会识别异常
            var->type = type_ptrof(var->type);
            slice_insert(params, 0, var);
        } else {
            assertf(return_type.kind != TYPE_ARRAY, "array type must be pointer type");

            // 申请栈空间，用于存储返回值, 返回值可能是一个小于 16byte 的 struct
            // 此时需要栈空间暂存返回值，然后在 fn_end 时将相应的值放到相应的寄存器上
            if (is_alloc_stack(return_type)) {
                lir_stack_alloc(c, result, return_type, c->return_operand);
            }
        }
    }

    linked_concat(result, amd64_lower_params(c, params));

    op->output->value = slice_new();
    return result;
}

/**
 * call test(arg1, aeg2, ...) -> result
 * @param c
 * @param op
 * @return
 */
static linked_t *amd6_lower_args(closure_t *c, slice_t *args, int64_t *stack_arg_size) {
    linked_t *result = linked_new();

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    uint8_t *onstack = mallocz(args->count + 1);
    lir_operand_t *rsp_operand = operand_new(LIR_OPERAND_REG, rsp);

    uint8_t sse_reg_count = 0;
    uint8_t int_reg_count = 0;

    *stack_arg_size = 0;

    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(arg_type, &lo, &hi, 0);
        if (count == 0) { // 返回 0 就是需要内存传递, 此时才需要几率栈内存！？
            *stack_arg_size += align_up(type_sizeof(arg_type), QWORD); // 参数按照 8byte 对齐
            onstack[i] = 1;
            continue;
        } else if (count == 1) {
            if (lo == AMD64_CLASS_SSE && sse_reg_count + 1 <= 8) {
                onstack[i] = 0;
                sse_reg_count++;
            } else if (lo == AMD64_CLASS_INTEGER && int_reg_count + 1 <= 6) {
                onstack[i] = 0;
                int_reg_count++;
            } else {
                *stack_arg_size += align_up(type_sizeof(arg_type), QWORD); // 参数按照 8byte 对齐
                onstack[i] = 1;
                continue;
            }
        } else if (count == 2) {
            if (lo == hi) {
                if (lo == AMD64_CLASS_SSE && sse_reg_count + 2 <= 8) {
                    onstack[i] = 0;
                    sse_reg_count += 2;
                } else if (lo == AMD64_CLASS_INTEGER && int_reg_count + 2 <= 6) {
                    onstack[i] = 0;
                    int_reg_count += 2;
                } else {
                    onstack[i] = 1;
                    *stack_arg_size += align_up(type_sizeof(arg_type), QWORD);
                }
            } else {
                if (sse_reg_count + 1 <= 8 && int_reg_count + 1 <= 6) {
                    onstack[i] = 0;
                    sse_reg_count++;
                    int_reg_count++;
                } else {
                    onstack[i] = 1;
                    *stack_arg_size += align_up(type_sizeof(arg_type), QWORD);
                }
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

        if (onstack[i] == 0) {
            continue;
        }

        type_t arg_type = lir_operand_type(arg);

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(arg_type, &lo, &hi, 0);

        uint16_t align_size = align_up(type_sizeof(arg_type), POINTER_SIZE);

        if (is_alloc_stack(arg_type)) {
            assert(arg->assert_type == LIR_OPERAND_VAR);
            // 通过 sub 预留足够的 stack 空间, 然后将 arg(var 中保存从是地址信息)
            linked_push(result,
                        lir_op_new(LIR_OPCODE_SUB, rsp_operand, int_operand(align_size), rsp_operand));

            linked_t *temps = lir_memory_mov(c->module, arg_type, rsp_operand, arg);
            linked_concat(result, temps);
        } else {
            linked_push(result, lir_op_new(LIR_OPCODE_SUB, rsp_operand, int_operand(align_size), rsp_operand));
            lir_operand_t *dst = indirect_addr_operand(c->module, arg_type, rsp_operand, 0);
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
        type_t arg_type = lir_operand_type(arg);

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(arg_type, &lo, &hi, 0);
        if (onstack[i] == 1) {
            continue;
        }

        assert(count != 0);

        if (arg_type.kind == TYPE_STRUCT) {
            // 将 arg 中的数据 mov 到 lo/hi 寄存器中
            lir_operand_t *lo_reg_operand;
            type_kind lo_kind;
            if (lo == AMD64_CLASS_INTEGER) {
                // 查找合适大小的 reg 进行 mov
                uint8_t reg_index = int_reg_indices[int_reg_index++];
                lo_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, TYPE_UINT64));
                lo_kind = TYPE_UINT64;
            } else if (lo == AMD64_CLASS_SSE) {
                uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                lo_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, TYPE_FLOAT64));
                lo_kind = TYPE_FLOAT64;
            } else {
                assert(false);
            }

            // arg 是第一个内存地址，现在需要读取其 indirect addr
            lir_operand_t *src_operand = indirect_addr_operand(c->module, type_kind_new(lo_kind), arg, 0);
            linked_push(result, lir_op_move(lo_reg_operand, src_operand));

            if (count == 2) {
                lir_operand_t *hi_reg_operand;
                type_kind hi_kind;
                if (hi == AMD64_CLASS_INTEGER) {
                    // 查找合适大小的 reg 进行 mov
                    uint8_t reg_index = int_reg_indices[int_reg_index++];
                    hi_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, TYPE_UINT64));
                    hi_kind = TYPE_UINT64;
                } else if (hi == AMD64_CLASS_SSE) {
                    uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                    hi_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, TYPE_FLOAT64));
                    hi_kind = TYPE_FLOAT64;
                } else {
                    assert(false);
                }

                // arg 是第一个内存地址，现在需要读取其 indirect addr
                src_operand = indirect_addr_operand(c->module, type_kind_new(hi_kind), arg, QWORD);
                linked_push(result, lir_op_move(hi_reg_operand, src_operand));
            }
        } else {
            assertf(count == 1, "the normal type uses only one register");
            // 从寄存器中将数据移入到低保空间
            lir_operand_t *lo_reg_operand;
            if (lo == AMD64_CLASS_INTEGER) {
                // 查找合适大小的 reg 进行 mov
                uint8_t reg_index = int_reg_indices[int_reg_index++];
                lo_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, arg_type.kind));
            } else if (lo == AMD64_CLASS_SSE) {
                uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                lo_reg_operand = operand_new(LIR_OPERAND_REG, amd64_reg_select(reg_index, arg_type.kind));
            } else {
                assert(false);
            }
            linked_push(result, lir_op_move(lo_reg_operand, arg));
        }
    }

    return result;
}

linked_t *amd64_lower_call(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    lir_operand_t *call_result = op->output;
    slice_t *args = op->second->value;
    lir_operand_t *rsp_operand = operand_new(LIR_OPERAND_REG, rsp);

    if (!call_result) {
        int64_t stack_arg_size = 0; // 参数 lower 占用的 stack size, 函数调用完成后需要还原
        linked_concat(result, amd6_lower_args(c, args, &stack_arg_size));
        op->second->value = slice_new();
        linked_push(result, op); // call op
        if (stack_arg_size > 0) {
            linked_push(result, lir_op_new(LIR_OPCODE_ADD, rsp_operand, int_operand(stack_arg_size), rsp_operand));
        }

        return result;
    }

    type_t result_type = lir_operand_type(call_result);

    // 进行 call result 的栈空间申请, 此时已经不会触发 ssa 了，可
    if (is_alloc_stack(result_type)) {
        assert(call_result->assert_type == LIR_OPERAND_VAR);
        uint64_t size = type_sizeof(result_type);
        c->stack_offset += align_up(size, QWORD);
        lir_operand_t *src_operand = lir_stack_operand(c->module, -c->stack_offset, size);

        linked_push(result, lir_op_lea(call_result, src_operand));
    }

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    int64_t count = amd64_type_classify(result_type, &lo, &hi, 0);

    // call result to args
    // count == 0 标识参数通过栈进行传递，但是需要注意的是此时传递并的是一个指针，而不是整个 struct
    // 如果把整个 struct 丢进去会造成识别异常
    if (count == 0) {
        type_t call_result_type = lir_operand_type(call_result);
        assertf(call_result->assert_type == LIR_OPERAND_VAR, "call result must a var");
        assert(is_alloc_stack(call_result_type));

        lir_operand_t *temp_arg = lir_operand_copy(call_result);
        lir_var_t *temp_var = temp_arg->value;
        temp_var->type = type_ptrof(temp_var->type);
        slice_insert(args, 0, temp_arg);
    }

    // -------------------- lower args ------------------------
    int64_t stack_arg_size = 0; // 参数 lower 占用的 stack size, 函数调用完成后需要还原
    linked_concat(result, amd6_lower_args(c, args, &stack_arg_size));
    op->second->value = slice_new();

    // 参数通过栈传递
    if (count == 0) {
        lir_operand_t *output_reg = operand_new(LIR_OPERAND_REG, amd64_reg_select(rax->index, TYPE_UINT64));
        linked_push(result, lir_op_new(LIR_OPCODE_CALL, op->first, op->second, output_reg));
        if (stack_arg_size > 0) {
            linked_push(result, lir_op_new(LIR_OPCODE_ADD, rsp_operand, int_operand(stack_arg_size), rsp_operand));
        }

        linked_push(result, lir_op_move(call_result, output_reg));
        return result;
    }

    // -- 返回值通过 reg 传递

    // call 指令 存在返回值， 现在需要处理相关的返回值,首先返回值必须通过 rax/rdi 返回
    lir_operand_t *lo_src_reg;
    type_kind lo_kind;
    if (lo == AMD64_CLASS_INTEGER) {
        lo_src_reg = operand_new(LIR_OPERAND_REG, rax);
        lo_kind = TYPE_UINT64;
    } else {
        lo_src_reg = operand_new(LIR_OPERAND_REG, xmm0s64);
        lo_kind = TYPE_FLOAT64;
    }

    // ~~ call_result 需要做替换，为了后续 interval 阶段能够正确收集 var def, 所以手动添加一下 def ~~ 上面的 lea 做了
    // linked_push(result, lir_op_nop(call_result));

    if (result_type.kind == TYPE_STRUCT) {
        // 无论如何，先生成 call 指令
        slice_t *output_regs = slice_new();
        slice_push(output_regs, lo_src_reg->value); // 不能延后处理，会导致 flag 设置异常！
        lir_operand_t *new_output = operand_new(LIR_OPERAND_REGS, output_regs);

        lir_operand_t *hi_src_reg;
        type_kind hi_kind;
        if (count == 2) {
            if (hi == AMD64_CLASS_INTEGER) {
                hi_src_reg = operand_new(LIR_OPERAND_REG, rdx);
                hi_kind = TYPE_UINT64;
            } else {
                hi_src_reg = operand_new(LIR_OPERAND_REG, xmm1s64);
                hi_kind = TYPE_FLOAT64;
            }
            slice_push(output_regs, hi_src_reg->value);
        }

        linked_push(result, lir_op_new(LIR_OPCODE_CALL, op->first, op->second, new_output));

        // 调用完成后进行栈归还
        if (stack_arg_size > 0) {
            linked_push(result, lir_op_new(LIR_OPCODE_ADD, rsp_operand, int_operand(stack_arg_size), rsp_operand));
        }

        // 从 reg 中将返回值 mov 到 call_result 上
        lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(lo_kind), call_result, 0);
        linked_push(result, lir_op_move(dst, lo_src_reg));
        if (count == 2) {
            dst = indirect_addr_operand(c->module, type_kind_new(hi_kind), call_result, QWORD);
            linked_push(result, lir_op_move(dst, hi_src_reg));
        }
    } else {
        assert(count == 1);
        reg_t *lo_reg = lo_src_reg->value;
        lo_src_reg = operand_new(LIR_OPERAND_REG, amd64_reg_select(lo_reg->index, result_type.kind));

        linked_push(result, lir_op_new(LIR_OPCODE_CALL, op->first, op->second, lo_src_reg));
        if (stack_arg_size > 0) {
            linked_push(result, lir_op_new(LIR_OPCODE_ADD, rsp_operand, int_operand(stack_arg_size), rsp_operand));
        }

        linked_push(result, lir_op_move(call_result, lo_src_reg));
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

int64_t amd64_type_classify(type_t t, amd64_class_t *lo, amd64_class_t *hi, uint64_t offset) {
    uint16_t size = type_sizeof(t);

    if (size > 16) {
        return 0;
    }

    if (t.kind == TYPE_ARRAY) {
        return 0; // 总是通过栈传递
    }

    if (is_float(t.kind)) {
        if (offset < 8) {
            *lo = amd64_classify_merge(*lo, AMD64_CLASS_SSE);
        } else {
            *hi = amd64_classify_merge(*hi, AMD64_CLASS_SSE);
        }
    } else if (t.kind == TYPE_STRUCT) {
        // 遍历 struct 的每一个属性进行分类
        type_struct_t *type_struct = t.struct_;
        offset = align_up(offset, type_struct->align); // 开始地址对齐

        for (int i = 0; i < type_struct->properties->length; i++) {
            struct_property_t *p = ct_list_value(type_struct->properties, i);
            type_t element_type = p->type;
            uint16_t element_size = type_sizeof(element_type);
            uint16_t element_align = element_size;
            if (p->type.kind == TYPE_STRUCT) {
                element_align = p->type.struct_->align;
            }

            // 每个元素地址对齐
            offset = align_up(offset, element_align);
            int64_t count = amd64_type_classify(element_type, lo, hi, offset);
            if (count == 0) {
                return 0;
            }

            offset += element_size;
        }

        // 最终 size 对齐
        offset = align_up(offset, type_struct->align);
    } else {
        if (offset < 8) {
            *lo = amd64_classify_merge(*lo, AMD64_CLASS_INTEGER);
        } else {
            *hi = amd64_classify_merge(*hi, AMD64_CLASS_INTEGER);
        }
    }

    return *hi == AMD64_CLASS_NO ? 1 : 2;
}
