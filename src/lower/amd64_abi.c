#include "amd64_abi.h"

#include "src/register/arch/amd64.h"

lir_operand_t *amd64_select_return_reg(lir_operand_t *operand) {
    type_kind kind = operand_type_kind(operand);
    if (kind == TYPE_FLOAT64) {
        return operand_new(LIR_OPERAND_REG, xmm0s64);
    }
    if (kind == TYPE_FLOAT32) {
        return operand_new(LIR_OPERAND_REG, xmm0s32);
    }

    return operand_new(LIR_OPERAND_REG, reg_select(rax->index, kind));
}

linked_t *amd64_lower_return(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();

    lir_operand_t *return_operand = op->first;
    if (!return_operand) {
        linked_push(result, op);
        return result;
    }

    type_t return_type = c->fndef->return_type;
    uint64_t return_size = type_sizeof(return_type);

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    int64_t count = amd64_type_classify(return_type, &lo, &hi, 0);

    if (count == 0) {
        // 通过寄存器进行传递，此时 c->return_operand 中存储了目标空间(可能是 caller 申请的内存空间，在 fn_begin 中将该空间的指针传递给了 c->return_operand)
        assert(c->return_big_operand);
        linked_t *temps = lir_memory_mov(c->module, return_size, c->return_big_operand, return_operand);
        linked_concat(result, temps);

        // 将 return big_operand 中的值存储在 rax 中
        lir_operand_t *rax_operand = operand_new(LIR_OPERAND_REG, rax);
        linked_push(result, lir_op_move(rax_operand, c->return_big_operand));

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

    if (return_type.kind == TYPE_STRUCT) {
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
        assertf(return_type.kind != TYPE_ARR, "array type must be pointer type");

        // 由于需要直接 mov， 所以还是需要选择合适的大小
        reg_t *lo_reg = lo_dst_reg->value;
        lo_dst_reg = operand_new(LIR_OPERAND_REG, reg_select(lo_reg->index, return_type.kind));

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

    int64_t stack_param_slot = 16; // by rbp, is_rest addr 和 push rsp 占用了 16 个字节

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
            // 最右侧参数先入栈，所以最右侧参数在栈顶，合理的方式是从左往右遍历读取 source 了。
            // 结构体由于 caller 已经申请了 stack 空间，所以参数中直接使用该空间的地址即可
            // 而不需要进行重复的 copy 和 stack 空间申请, 这也导致 gc_malloc 功能无法正常使用。
            lir_operand_t *src = lir_stack_operand(c->module, stack_param_slot, type_sizeof(param_type),
                                                   param_type.kind);

            // stack 会被 native 成 indirect_addr [rbp+slot], 如果其中保存的是结构体，这里需要的是地址。
            if (is_stack_ref_big_type(param_type)) {
                lir_operand_t *src_ref = lower_temp_var_operand(c, result, type_kind_new(TYPE_ANYPTR));
                linked_push(result, lir_op_lea(src_ref, src));
                linked_push(result, lir_op_move(dst_param, src_ref));
            } else {
                linked_push(result, lir_op_move(dst_param, src));
            }

            stack_param_slot += align_up(type_sizeof(param_type), QWORD); // 参数按照 8byte 对齐
        } else {
            if (param_type.kind == TYPE_STRUCT) {
                linked_push(result, lir_stack_alloc(c, param_type, dst_param));

                // 从寄存器中将数据移入到低保空间
                lir_operand_t *lo_reg_operand;
                type_kind lo_kind;
                if (lo == AMD64_CLASS_INTEGER) {
                    // 查找合适大小的 reg 进行 mov
                    uint8_t reg_index = int_reg_indices[int_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, TYPE_UINT64));
                    lo_kind = TYPE_UINT64;
                } else if (lo == AMD64_CLASS_SSE) {
                    uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, TYPE_FLOAT64));
                    lo_kind = TYPE_FLOAT64;
                } else {
                    assert(false);
                }

                lir_operand_t *lo_dst = indirect_addr_operand(c->module, type_kind_new(lo_kind), dst_param, 0);

                linked_push(result, lir_op_move(lo_dst, lo_reg_operand));

                if (count == 2) {
                    // 从寄存器中将数据移入到低保空间
                    lir_operand_t *hi_reg_operand;
                    type_kind hi_kind;
                    if (hi == AMD64_CLASS_INTEGER) {
                        // 查找合适大小的 reg 进行 mov
                        uint8_t reg_index = int_reg_indices[int_reg_index++];
                        hi_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, TYPE_UINT64));
                        hi_kind = TYPE_UINT64;
                    } else if (hi == AMD64_CLASS_SSE) {
                        uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                        hi_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, TYPE_FLOAT64));
                        hi_kind = TYPE_FLOAT64;
                    } else {
                        assert(false);
                    }

                    lir_operand_t *hi_dst = indirect_addr_operand(c->module, type_kind_new(lo_kind), dst_param, QWORD);

                    linked_push(result, lir_op_move(hi_dst, hi_reg_operand));
                }
            } else {
                assertf(param_type.kind != TYPE_ARR, "array type must be pointer type");
                assertf(count == 1, "the normal type uses only one register");
                // 从寄存器中将数据移入到低保空间
                lir_operand_t *lo_reg_operand;
                uint8_t reg_index;
                if (lo == AMD64_CLASS_INTEGER) {
                    // 查找合适大小的 reg 进行 mov
                    reg_index = int_reg_indices[int_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, param_type.kind));
                } else if (lo == AMD64_CLASS_SSE) {
                    reg_index = sse_reg_indices[sse_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, param_type.kind));
                } else {
                    assert(false);
                }

                linked_push(result, lir_op_move(dst_param, lo_reg_operand));
            }
        }
    }

    return result;
}

linked_t *amd64_lower_fn_begin(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    linked_push(result, op); // 将 FN_BEGIN 放在最前端，后续 native 还需要使用

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    slice_t *params = op->output->value;

    type_t return_type = c->fndef->return_type;

    if (return_type.kind != TYPE_VOID) {
        // 为了 ssa 将 return var 添加到了 params 中，现在 从 params 中移除, 使用 nop 或者 其他方式保证 def 完整
        int64_t count = amd64_type_classify(return_type, &lo, &hi, 0);

        if (count == 0) { // count == 0 则无法通过寄存器传递返回值
            assert(return_type.kind == TYPE_STRUCT || return_type.kind == TYPE_ARR);

            // return operand 总是通过寄存器 rax 或特殊寄存器返回。
            c->return_big_operand = temp_var_operand(c->module, type_kind_new(TYPE_ANYPTR));

            lir_operand_t *return_operand = c->return_big_operand;

            // 占用 rdi 寄存器, 在 lower_params 中会进行 move rdi -> return_operand_var
            slice_insert(params, 0, return_operand->value);
        } else {
            assertf(return_type.kind != TYPE_ARR, "array type must be pointer type");
        }
    }

    linked_concat(result, amd64_lower_params(c, params));
    op->output->value = slice_new();
    return result;
}

/**
 * call test(arg1, aeg2, ...) -> result
 * 使用到的寄存器需要收集起来，放到 call->second 保持 reg 的声明返回健康
 * @param c
 * @param op
 * @return
 */
static linked_t *amd64_lower_args(closure_t *c, lir_op_t *op) {
    slice_t *args = op->second->value; // exprs

    // 进行 op 替换(重新set flag 即可)
    slice_t *use_regs = slice_new(); // reg_t*

    linked_t *result = linked_new();

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    uint8_t *onstack = mallocz(args->count + 1);
    lir_operand_t *sp_operand = operand_new(LIR_OPERAND_REG, rsp);

    uint8_t sse_reg_count = 0;
    uint8_t int_reg_count = 0;

    int64_t stack_offset = 0;

    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(arg_type, &lo, &hi, 0);
        if (count == 0) {
            // 返回 0 就是需要内存传递, 此时才需要几率栈内存！？
            stack_offset += align_up(type_sizeof(arg_type), QWORD); // 参数按照 8byte 对齐
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
                stack_offset += align_up(type_sizeof(arg_type), QWORD); // 参数按照 8byte 对齐
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
                    stack_offset += align_up(type_sizeof(arg_type), QWORD);
                }
            } else {
                if (sse_reg_count + 1 <= 8 && int_reg_count + 1 <= 6) {
                    onstack[i] = 0;
                    sse_reg_count++;
                    int_reg_count++;
                } else {
                    onstack[i] = 1;
                    stack_offset += align_up(type_sizeof(arg_type), QWORD);
                }
            }
        }
    }

    // native 时会进行 16byte 对齐
    if (stack_offset > c->call_stack_max_offset) {
        c->call_stack_max_offset = stack_offset;
    }

    // 最右侧参数最先入栈， sub 相当于入栈标识
    // 如果从最左侧的第一个需要入栈的参数开始处理，则其对应的就是 rsp+0
    // 优先处理需要通过 stack 传递的参数,将他们都 push 或者 mov 到栈里面(越靠右的参数越优先入栈), 使用 mov 的话就不需要担心先后问题
    int64_t rsp_offset = 0;
    for (int i = 0; i < args->count; i++) {
        lir_operand_t *arg_operand = args->take[i];

        if (onstack[i] == 0) {
            continue;
        }

        type_t arg_type = lir_operand_type(arg_operand);

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(arg_type, &lo, &hi, 0);

        lir_operand_t *dst = indirect_addr_operand(c->module, arg_type, sp_operand, rsp_offset);

        if (is_stack_ref_big_type(arg_type)) {
            assert(arg_operand->assert_type == LIR_OPERAND_VAR);
            // lea addr to temp
            lir_operand_t *dst_ref = lower_temp_var_operand(c, result, type_kind_new(TYPE_ANYPTR));
            linked_push(result, lir_op_lea(dst_ref, dst));

            linked_t *temps = lir_memory_mov(c->module, type_sizeof(arg_type), dst_ref, arg_operand);
            linked_concat(result, temps);
        } else {
            linked_push(result, lir_op_move(dst, arg_operand));
        }

        // 已经在栈上为大结构体预留预留了足够的空间
        uint64_t align_size = align_up(type_sizeof(arg_type), POINTER_SIZE);
        rsp_offset += align_size;
    }

    // - 处理通过寄存器传递的参数(选择合适的寄存器。多余的部分进行 xor 清空)
    uint8_t int_reg_indices[6] = {7, 6, 2, 1, 8, 9};
    uint8_t sse_reg_indices[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t int_reg_index = 0;
    uint8_t sse_reg_index = 0;

    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);

        lo = AMD64_CLASS_NO;
        hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(arg_type, &lo, &hi, 0);
        if (onstack[i] == 1) {
            continue;
        }

        assert(count != 0);

        if (arg_type.kind == TYPE_STRUCT) {
            lir_operand_t *lo_temp_var;
            type_kind lo_kind;
            if (lo == AMD64_CLASS_INTEGER) {
                uint8_t reg_index = int_reg_indices[int_reg_index++];
                reg_t *hint_reg = reg_select(reg_index, TYPE_UINT64);
                lo_kind = TYPE_UINT64;
                lo_temp_var = lower_temp_var_with_hint(c, result, type_kind_new(lo_kind), hint_reg);
            } else if (lo == AMD64_CLASS_SSE) {
                uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                reg_t *hint_reg = reg_select(reg_index, TYPE_FLOAT64);
                lo_kind = TYPE_FLOAT64;
                lo_temp_var = lower_temp_var_with_hint(c, result, type_kind_new(lo_kind), hint_reg);
            } else {
                assert(false);
                exit(EXIT_FAILURE);
            }

            lir_operand_t *src_operand = indirect_addr_operand(c->module, type_kind_new(lo_kind), arg, 0);
            linked_push(result, lir_op_call_arg_move(lo_temp_var, src_operand));
            slice_push(use_regs, lo_temp_var->value);

            if (count == 2) {
                lir_operand_t *hi_temp_var;
                type_kind hi_kind;
                if (hi == AMD64_CLASS_INTEGER) {
                    uint8_t reg_index = int_reg_indices[int_reg_index++];
                    reg_t *hint_reg = reg_select(reg_index, TYPE_UINT64);
                    hi_kind = TYPE_UINT64;
                    hi_temp_var = lower_temp_var_with_hint(c, result, type_kind_new(hi_kind), hint_reg);
                } else if (hi == AMD64_CLASS_SSE) {
                    uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                    reg_t *hint_reg = reg_select(reg_index, TYPE_FLOAT64);
                    hi_kind = TYPE_FLOAT64;
                    hi_temp_var = lower_temp_var_with_hint(c, result, type_kind_new(hi_kind), hint_reg);
                } else {
                    assert(false);
                    exit(EXIT_FAILURE);
                }

                src_operand = indirect_addr_operand(c->module, type_kind_new(hi_kind), arg, QWORD);
                linked_push(result, lir_op_call_arg_move(hi_temp_var, src_operand));
                slice_push(use_regs, hi_temp_var->value);
            }
        } else {
            assertf(count == 1, "the normal type uses only one register");
            lir_operand_t *lo_temp_var;
            if (lo == AMD64_CLASS_INTEGER) {
                uint8_t reg_index = int_reg_indices[int_reg_index++];
                reg_t *hint_reg = reg_select(reg_index, arg_type.kind);
                lo_temp_var = lower_temp_var_with_hint(c, result, arg_type, hint_reg);
            } else if (lo == AMD64_CLASS_SSE) {
                uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                reg_t *hint_reg = reg_select(reg_index, arg_type.kind);
                lo_temp_var = lower_temp_var_with_hint(c, result, arg_type, hint_reg);
            } else {
                assert(false);
            }
            linked_push(result, lir_op_call_arg_move(lo_temp_var, arg));
            slice_push(use_regs, lo_temp_var->value);
        }
    }

    op->second = lir_reset_operand(operand_new(LIR_OPERAND_VARS, use_regs), LIR_FLAG_SECOND);
    set_operand_flag(op->second);

    return result;
}

linked_t *amd64_lower_call(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    lir_operand_t *call_result = op->output;
    lir_operand_t *rsp_operand = operand_new(LIR_OPERAND_REG, rsp);

    if (!call_result) {
        linked_concat(result, amd64_lower_args(c, op));

        // 处理 call first operand (函数指针) 的 must_hint
        // 当 first 是 VAR 时，需要为其分配一个不参与 args_abi 的固定寄存器，避免与参数冲突
        if (op->first && op->first->assert_type == LIR_OPERAND_VAR) {
            lir_var_t *first_var = op->first->value;
            type_t first_type = first_var->type;
            lir_operand_t *first_temp_var = lower_temp_var_with_hint(c, result, first_type, r11);
            linked_push(result, lir_op_call_arg_move(first_temp_var, op->first));
            op->first = lir_reset_operand(first_temp_var, LIR_FLAG_FIRST);
        }

        linked_push(result, op); // call op

        return result;
    }

    type_t call_result_type = lir_operand_type(call_result);

    // amd64 中，大型返回值由 caller 申请空间，并将空间指针通过 rdi 寄存器传递给 calle
    if (is_stack_ref_big_type(call_result_type)) {
        assert(call_result->assert_type == LIR_OPERAND_VAR);

        linked_push(result, lir_stack_alloc(c, call_result_type, call_result));
    }

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    int64_t count = amd64_type_classify(call_result_type, &lo, &hi, 0);

    // call result to args
    // count == 0 标识参数通过栈进行传递，但是需要注意的是此时传递并的是一个指针，而不是整个 struct
    // 如果把整个 struct 丢进去会造成识别异常
    if (count == 0) {
        assertf(call_result->assert_type == LIR_OPERAND_VAR, "call result must a var");
        assert(is_stack_ref_big_type(lir_operand_type(call_result)));

        lir_operand_t *temp_arg = lir_operand_copy(call_result);
        lir_var_t *temp_var = temp_arg->value;
        temp_var->type = type_ptrof(temp_var->type);
        slice_insert(op->second->value, 0, temp_arg); // 将 call result 也填入到 args 中进行统一处理
    }

    // -------------------- lower args ------------------------
    linked_concat(result, amd64_lower_args(c, op));

    // 处理 call first operand (函数指针) 的 must_hint
    // 当 first 是 VAR 时，需要为其分配一个不参与 args_abi 的固定寄存器，避免与参数冲突
    if (op->first && op->first->assert_type == LIR_OPERAND_VAR) {
        lir_var_t *first_var = op->first->value;
        type_t first_type = first_var->type;
        lir_operand_t *first_temp_var = lower_temp_var_with_hint(c, result, first_type, r11);
        linked_push(result, lir_op_call_arg_move(first_temp_var, op->first));
        op->first = lir_reset_operand(first_temp_var, LIR_FLAG_FIRST);
    }

    // 参数通过栈传递, 返回值处理
    if (count == 0) {
        lir_operand_t *output_reg = operand_new(LIR_OPERAND_REG, reg_select(rax->index, TYPE_UINT64));
        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, output_reg, op->line, op->column));
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

    if (call_result_type.kind == TYPE_STRUCT) {
        // 无论如何，先生成 call 指令
        slice_t *output_regs = slice_new();
        slice_push(output_regs, lo_src_reg->value); // 不能延后处理，会导致 flag 设置异常！
        lir_operand_t *new_output = lir_reset_operand(operand_new(LIR_OPERAND_REGS, output_regs), LIR_FLAG_OUTPUT);

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

        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, new_output, op->line, op->column));

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
        lo_src_reg = operand_new(LIR_OPERAND_REG, reg_select(lo_reg->index, call_result_type.kind));

        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, lo_src_reg, op->line, op->column));

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
    uint64_t size = type_sizeof(t);

    if (size > 16) {
        return 0;
    }

    // 在 nature 实现中，栈总是通过栈传递并进行完整的值传递
    // 而 c 语言中，arr 本身是在 abi 中未定义的数据类型，所以 arr 需要作为 ptr 进行传递
    if (t.kind == TYPE_ARR) {
        return 0;
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
        offset = align_up(offset, type_struct_alignof(type_struct)); // 开始地址对齐

        for (int i = 0; i < type_struct->properties->length; i++) {
            struct_property_t *p = ct_list_value(type_struct->properties, i);
            type_t element_type = p->type;
            uint64_t element_size = type_sizeof(element_type);
            uint64_t element_align = element_size;
            if (p->type.kind == TYPE_STRUCT) {
                element_align = type_struct_alignof(p->type.struct_);
            } else if (p->type.kind == TYPE_ARR) {
                element_align = type_sizeof(p->type.array->element_type);
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
        offset = align_up(offset, type_struct_alignof(type_struct));
    } else {
        if (offset < 8) {
            *lo = amd64_classify_merge(*lo, AMD64_CLASS_INTEGER);
        } else {
            *hi = amd64_classify_merge(*hi, AMD64_CLASS_INTEGER);
        }
    }

    return *hi == AMD64_CLASS_NO ? 1 : 2;
}
