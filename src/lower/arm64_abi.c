#include "arm64_abi.h"
#include "src/register/arch/arm64.h"

static int arm64_hfa_aux(type_t type, int32_t *fsize, int num) {
    assert(fsize != NULL);
    assert(num >= 0);

    uint16_t size = type_sizeof(type);

    if (is_float(type.kind)) {
        uint16_t n = size;
        uint16_t a = type_alignof(type);
        if (num >= 4 || (*fsize && *fsize != n)) {
            return -1;
        }
        *fsize = n;
        return num + 1;
    } else if (type.kind == TYPE_STRUCT) {
        int is_struct = 1; // nature 中只有结构体

        // 遍历 struct
        type_struct_t *s = type.struct_;
        assert(s != NULL);
        assert(s->properties != NULL);

        int num0 = num;
        uint16_t offset = 0;

        for (int i = 0; i < s->properties->length; ++i) {
            struct_property_t *p = ct_list_value(s->properties, i);
            assert(p != NULL);
            uint16_t element_size = type_sizeof(p->type);
            uint16_t element_align = type_alignof(p->type);
            offset = align_up(offset, element_align);
            if (offset != (num - num0) * *fsize) {
                return -1;
            }

            num = arm64_hfa_aux(p->type, fsize, num);
            if (num == -1) {
                return -1;
            }

            offset += element_size;
        }

        if (size != (num - num0) * *fsize) {
            return -1;
        }
        return num;
    }

    // ARR 总是通过栈传递
    return -1;
}

static int arm64_hfa(type_t type, int32_t *fsize) {
    if (type.kind == TYPE_STRUCT || (type.kind == TYPE_ARR && type.in_heap == false)) {
        int sz = 0;
        int n = arm64_hfa_aux(type, &sz, 0);
        if (0 < n && n <= 4) {
            if (fsize) {
                *fsize = sz;
            }
            return n;
        }
    }
    return 0;
}

static int64_t arm64_pcs_aux(int64_t n, type_t *args_types, int64_t *args_pos) {
    int nx = 0; // 下一个整数寄存器
    int nv = 0; // 下一个向量寄存器
    int64_t ns = 32; // 下一个栈偏移, 前面 32 个数字被寄存器和值传递类型(值传递 &1=0或者指针传递&1=1)占用
    int i;

    for (i = 0; i < n; i++) {
        int hfa = arm64_hfa(args_types[i], 0);
        uint16_t size;
        uint16_t align;

        // 数组总是作为指针进行处理
        if (args_types[i].kind == TYPE_FN) {
            size = align = 8;
        } else {
            size = type_sizeof(args_types[i]);
            align = type_alignof(args_types[i]);
        }

        if (hfa) {
        } else if (size > 16 || args_types[i].kind == TYPE_ARR) {
            // type_arr 总是作为大型结构体进行处理
            // 大于16字节的结构体通过指针传递, 如果有空余的寄存器(nx < 8)，则指针需要占用一个寄存器
            if (nx < 8) {
                args_pos[i] = (nx++ << 1) | 1;
            } else {
                // 指针被推入栈中
                ns = align_up(ns, 8);
                args_pos[i] = ns | 1; // |1 标记大型结构体
                ns += 8;
            }

            continue;
        } else if (args_types[i].kind == TYPE_STRUCT) {
            // struct 但是 size < 16
            // B.4
            // 结构体
            size = (size + 7) & ~7;
        }


        // c.1
        if (is_float(args_types[i].kind)) {
            args_pos[i] = 16 + (nv++ << 1);
            continue;
        }

        // c.2
        if (hfa && nv + hfa <= 8) {
            args_pos[i] = 16 + (nv << 1);
            nv += hfa;
            continue;
        }

        // C.3
        if (hfa) {
            nv = 8;
            size = (size + 7) & ~7;
        }

        // C.4 未实现 LDOUBLE 类型

        // C.5
        if (args_types[i].kind == TYPE_FLOAT32) {
            size = 8;
        }

        // C.6
        if (hfa || is_float(args_types[i].kind)) {
            args_pos[i] = ns;
            ns += size;
            continue;
        }

        // C.7
        if (args_types[i].kind != TYPE_STRUCT && size <= 8 && nx < 8) {
            args_pos[i] = nx++ << 1;
            continue;
        }

        // C.8
        if (align == 16) {
            nx = (nx + 1) & ~1;
        }

        // C.9
        if (args_types[i].kind != TYPE_STRUCT && size == 16 && nx < 7) {
            args_pos[i] = nx << 1;
            nx += 2;
            continue;
        }

        // C.10
        if (args_types[i].kind == TYPE_STRUCT && size <= (8 - nx) * 8) {
            args_pos[i] = nx << 1;
            nx += (size + 7) >> 3;
            continue;
        }

        // C.11
        nx = 8;

        // C.12
        ns = align_up(ns, 8);
        ns = (ns + align - 1) & -align;

        // C.13
        if (args_types[i].kind == TYPE_STRUCT) {
            args_pos[i] = ns;
            ns += size;
            continue;
        }

        // C.14
        if (size < 8) {
            size = 8;
        }

        // C.15
        args_pos[i] = ns;
        ns += size;
    }

    return ns - 32; // 返回占用的总的栈空间
}

/**
 * 根据 arm64 abi 将寄存器中的参数传递到 param 中。
 */
static linked_t *arm64_lower_params(closure_t *c, slice_t *param_vars) {
    assert(c != NULL);
    assert(param_vars != NULL);

    linked_t *result = linked_new();

    if (param_vars->count == 0) {
        return result;
    }

    int64_t *args_pos = mallocz(param_vars->count * sizeof(int64_t));
    type_t *args_type = mallocz(param_vars->count * sizeof(type_t));
    assert(args_pos != NULL);
    assert(args_type != NULL);

    for (int i = 0; i < param_vars->count; ++i) {
        lir_var_t *var = param_vars->take[i];
        assert(var != NULL);
        args_type[i] = var->type;
    }

    int64_t stack_offset = arm64_pcs_aux(param_vars->count, args_type, args_pos);

    for (int i = 0; i < param_vars->count; ++i) {
        lir_var_t *var = param_vars->take[i];
        type_t param_type = var->type;
        lir_operand_t *dst_param = operand_new(LIR_OPERAND_VAR, var);

        // arg_pos 中存储了当前参数的具体位置, 不需要关心返回值
        int64_t arg_pos = args_pos[i];

        if (arg_pos < 16) {
            // 通过整数寄存器传递

            if (param_type.kind == TYPE_STRUCT && !(arg_pos & 1)) {
                // 位  param 申请足够的栈空间来接收寄存器中传递的结构体参数
                linked_push(result, lir_stack_alloc(c, param_type, dst_param)); // dst_param def

                // 结构体可能占用两个寄存器
                // 结构体存储在寄存器中
                lir_operand_t *lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(arg_pos >> 1, TYPE_UINT64));
                lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), dst_param, 0);
                linked_push(result, lir_op_move(dst, lo_reg_operand));

                if (type_sizeof(param_type) > 8) {
                    lir_operand_t *hi_reg_operand = operand_new(LIR_OPERAND_REG,
                                                                reg_select((arg_pos >> 1) + 1, TYPE_UINT64));
                    dst = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), dst_param, 8);
                    linked_push(result, lir_op_move(dst, hi_reg_operand));
                }
            } else {
                uint8_t reg_index = arg_pos >> 1;
                lir_operand_t *src = operand_new(LIR_OPERAND_REG, reg_select(reg_index, param_type.kind));
                linked_push(result, lir_op_move(dst_param, src)); // dst_param def

                // fn runtime operand 就是一个 type_fn 类型的指针
                if (c->fn_runtime_operand != NULL && i == param_vars->count - 1) {
                    c->fn_runtime_reg = reg_index;
                }
            }
        } else if (arg_pos < 32) {
            // 通过浮点寄存器传递

            int64_t reg_index = (arg_pos >> 1) - 8;
            if (param_type.kind == TYPE_STRUCT) {
                // 为 dst_param 分配栈空间
                linked_push(result, lir_stack_alloc(c, param_type, dst_param));

                int32_t fsize = 0;
                int32_t n = arm64_hfa(param_type, &fsize);
                assert(n <= 4);
                type_kind ftype_kind = (fsize == 4) ? TYPE_FLOAT32 : TYPE_FLOAT64;

                for (int j = 0; j < n; ++j) {
                    lir_operand_t *src = operand_new(LIR_OPERAND_REG, reg_select(reg_index + j, ftype_kind));
                    lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(ftype_kind),
                                                               dst_param, // not def
                                                               j * fsize);
                    linked_push(result, lir_op_move(dst, src));
                }
            } else {
                lir_operand_t *src = operand_new(LIR_OPERAND_REG, reg_select(reg_index, param_type.kind));
                linked_push(result, lir_op_move(dst_param, src)); // def
            }
        } else {
            // 参数通过栈传递 stack pass (过大的结构体通过栈指针+寄存器的方式传递)

            int64_t sp_offset = arg_pos - 32 + 16; // 16是为保存的fp和lr预留的空间
            lir_operand_t *src = lir_stack_operand(c->module, sp_offset, type_sizeof(param_type), param_type.kind);

            if ((arg_pos & 1) || (type_sizeof(param_type) <= 8)) {
                linked_push(result, lir_op_move(dst_param, src)); // dst_param def
            } else {
                assert(is_stack_ref_big_type(param_type));
                assert(type_sizeof(param_type) <= 16 && type_sizeof(param_type) > 8);

                lir_operand_t *src_ref = lower_temp_var_operand(c, result, type_kind_new(TYPE_ANYPTR));
                linked_push(result, lir_op_lea(src_ref, src));

                // 直接移动栈指针，而不是进行完全的 copy
                linked_push(result, lir_op_move(dst_param, src_ref));
            }

            // 记录最后一个参数所在的栈起点(fn_runtime_operand)
            if (c->fn_runtime_operand != NULL && i == param_vars->count - 1) {
                assert(sp_offset > 0);
                c->fn_runtime_stack = sp_offset;
            }
        }
    }

    free(args_pos);
    free(args_type);

    return result;
}

linked_t *arm64_lower_fn_begin(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    linked_push(result, op); // 将 FN_BEGIN 放在最前端，后续 native 还需要使用

    slice_t *params = op->output->value;

    if (c->return_operand) {
        // 移除最后一个参数，其指向了 return_operand
        slice_remove(params, params->count - 1);

        type_t return_type = lir_operand_type(c->return_operand);
        int32_t fsize = 0;
        int32_t hfa_count = arm64_hfa(return_type, &fsize);

        if (hfa_count > 0 && hfa_count <= 4) {
            // HFA 返回值不需要特殊处理，直接使用 v0-v3 寄存器
            // 增加 nop 指令让参数更加完整
            linked_push(result, lir_op_output(LIR_OPCODE_NOP, c->return_operand));
        } else if (return_type.kind == TYPE_ARR || type_sizeof(return_type) > 16) {
            assert(return_type.kind == TYPE_STRUCT || return_type.kind == TYPE_ARR);

            // 数组由于调用约定没有明确规定，所以我们也按照类似的处理方式
            // 大于 16 字节的结构体，通过内存返回，x8 中存储返回地址
            lir_operand_t *return_operand = c->return_operand;
            assert(return_operand->assert_type == LIR_OPERAND_VAR);

            // 从 x8 寄存器获取返回地址
            lir_operand_t *x8_reg = operand_new(LIR_OPERAND_REG, x8);
            linked_push(result, lir_op_move(return_operand, x8_reg));
        } else {
            // 小于等于 16 字节的结构体或基本类型，不需要特殊处理
            if (return_type.kind == TYPE_STRUCT) {
                linked_push(result, lir_stack_alloc(c, return_type, c->return_operand));
            } else {
                linked_push(result, lir_op_output(LIR_OPCODE_NOP, c->return_operand));
            }
        }
    }

    linked_concat(result, arm64_lower_params(c, params));
    op->output->value = slice_new();
    return result;
}

linked_t *arm64_lower_call(closure_t *c, lir_op_t *op) {
    assert(c != NULL);
    assert(op != NULL);
    assert(op->second != NULL);

    linked_t *result = linked_new();
    slice_t *args = op->second->value; // exprs
    assert(args);

    int64_t args_count = args->count; // 不需要处理可变参数
    assert(args_count >= 0);
    lir_operand_t *sp_operand = operand_new(LIR_OPERAND_REG, sp);

    lir_operand_t *call_result = op->output;
    type_t call_result_type = type_kind_new(TYPE_VOID);
    if (call_result) {
        call_result_type = lir_operand_type(call_result);
    }

    int64_t *args_pos = mallocz((args_count + 1) * sizeof(int64_t));
    assert(args_pos);
    type_t *args_type = mallocz(sizeof(type_t) * (args_count + 1));

    args_type[0] = call_result_type;
    for (int i = 0; i < args_count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);
        args_type[i + 1] = arg_type;
    }

    // args_pos[0] 存储着返回值的存储类型，如果没有返回值，则 arg_pos[0] = -1
    if (args_type[0].kind == TYPE_VOID) {
        args_pos[0] = -1;
    } else {
        // 计算 call result pos
        arm64_pcs_aux(1, args_type, args_pos);
        assert(args_pos[0] == 0 | args_pos[0] == 1 || args_pos[0] == 16); // v0 或者 x0 寄存器
        if (args_type[0].kind == TYPE_ARR) {
            assert(args_pos[0] == 1);
        }
    }

    // +1 表示跳过 call_result
    int64_t stack_offset = arm64_pcs_aux(args_count, args_type + 1, args_pos + 1);

    // 进行栈空间最大值预留
    // 已经计算出了参数传递需要占用的 stack_temp_offset, 为 stack_offset 赋值
    // 为什么进行 比较，是因为当前 fn 中可能存在多个 call 需要占用栈空间，取最大值
    stack_offset = align_up(stack_offset, 16);
    if (stack_offset > c->call_stack_max_offset) {
        c->call_stack_max_offset = stack_offset;
    }

    // 处理所有的通过指针传递的数据，为其在当前栈帧中分配栈空间 (基于 c->stack_offset 的永久栈空间)
    for (int i = 0; i < args_count; ++i) {
        lir_operand_t *arg_operand = args->take[i];
        int64_t arg_pos = args_pos[i + 1]; // 跳过 fn result
        type_t arg_type = lir_operand_type(arg_operand);
        if (arg_pos & 1) {
            // sub rsp -> size
            // mov arg -> rsp_offset
            // replace to new temp_point_operand
            lir_operand_t *stack_ptr_operand = lower_temp_var_operand(c, result, arg_type);
            linked_t *temps = lir_memory_mov(c->module, type_sizeof(arg_type), stack_ptr_operand, arg_operand);
            linked_concat(result, temps);
            args->take[i] = stack_ptr_operand; // 参数替换, 类型依旧不变
        }
    }


    // first pass: 对 stack 参数进行处理
    for (int64_t i = 0; i < args_count; i++) {
        lir_operand_t *arg_operand = args->take[i];
        type_t arg_type = lir_operand_type(arg_operand);
        int64_t arg_item_pos = args_pos[i + 1];

        if (arg_item_pos < 32) {
            continue;
        }

        int64_t sp_offset = arg_item_pos - 32;

        // sp operand
        lir_operand_t *dst = indirect_addr_operand(c->module, arg_type, sp_operand, sp_offset);

        uint16_t size = type_sizeof(arg_type);

        if ((arg_item_pos & 1) || (size <= 8)) {
            linked_push(result, lir_op_move(dst, arg_operand));
        } else {
            // struct or arr 但是数据小于 8
            assert(size <= 16 && size > 8);
            lir_operand_t *dst_ref = lower_temp_var_operand(c, result, type_kind_new(TYPE_ANYPTR));
            linked_push(result, lir_op_lea(dst_ref, dst));

            linked_t *temps = lir_memory_mov(c->module, type_sizeof(arg_type), dst_ref, arg_operand);
            linked_concat(result, temps);
        }
    }

    slice_t *use_regs = slice_new(); // reg_t

    // second pass: register 参数处理
    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg_operand = args->take[i];
        type_t arg_type = lir_operand_type(arg_operand);
        uint16_t size = type_sizeof(arg_type);

        int64_t arg_pos = args_pos[i + 1];
        int64_t reg_index = arg_pos >> 1;

        if (arg_pos < 16) {
            // 直接存储在通用寄存器中
            if (arg_type.kind == TYPE_STRUCT && !(arg_pos & 1)) {
                // 结构体存储在寄存器中
                lir_operand_t *lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, TYPE_UINT64));
                type_t lo_type = type_kind_new(TYPE_UINT64);
                // 结构体如果超过 8byte 则需要两个连续的寄存器，默认已经分配了连续寄存器， reg+1, 现在先处理小于 8byte 的部分
                // 结构体中可能存在多个成员共用一个 8byte 寄存器，所以这里统一使用 UINT64 进行 8byte 的整体移动。一旦超过 8byte 则进行下一个 8byte 的整体移动
                lir_operand_t *src_operand = indirect_addr_operand(c->module, lo_type, arg_operand, 0);
                linked_push(result, lir_op_move(lo_reg_operand, src_operand));
                slice_push(use_regs, lo_reg_operand->value);

                if (size > 8) {
                    assert(size <= 16);
                    lir_operand_t *hi_reg_operand = operand_new(LIR_OPERAND_REG,
                                                                reg_select(reg_index + 1, TYPE_UINT64));
                    type_t hi_type = type_kind_new(TYPE_UINT64);
                    src_operand = indirect_addr_operand(c->module, hi_type, arg_operand, QWORD);
                    linked_push(result, lir_op_move(hi_reg_operand, src_operand));
                    slice_push(use_regs, hi_reg_operand->value);
                }
            } else {
                // 进行合适大小选择
                lir_operand_t *lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, arg_type.kind));
                linked_push(result, lir_op_move(lo_reg_operand, arg_operand));
                slice_push(use_regs, lo_reg_operand->value);
            }
        } else if (arg_pos < 32) {
            // struct 通过指针传递时，分配的寄存器总是 < 16 的通用寄存器
            reg_index = (arg_pos >> 1) - 8; // 计算正确的浮点寄存器索引

            // arg 通过浮点形寄存器传递
            if (arg_type.kind == TYPE_STRUCT) {
                int32_t fsize = 0;
                int32_t n = arm64_hfa(arg_type, &fsize); // 返回 hfa 数量
                assert(n <= 4);
                assert(fsize == 4 || fsize == 8);
                type_kind ftype_kind = TYPE_FLOAT32;
                if (fsize == 8) {
                    ftype_kind = TYPE_FLOAT64;
                }

                int64_t struct_offset = 0;
                for (int j = 0; j < n; ++j) {
                    struct_offset += fsize;

                    lir_operand_t *dst_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index + j, ftype_kind));
                    lir_operand_t *src_operand = indirect_addr_operand(c->module, type_kind_new(ftype_kind),
                                                                       arg_operand, struct_offset);

                    linked_push(result, lir_op_move(dst_operand, src_operand));
                    slice_push(use_regs, dst_operand->value);
                }
            } else {
                lir_operand_t *dst_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, arg_type.kind));
                linked_push(result, lir_op_move(dst_operand, arg_operand));
                slice_push(use_regs, dst_operand->value);
            }
        }
    }

    // 重新生成 op->second, 用于寄存器分配记录
    op->second = operand_new(LIR_OPERAND_REGS, use_regs);
    set_operand_flag(op->second);

    // 基于 return type 的不同类型生成不同的 call
    if (!call_result) {
        linked_push(result, op); // call_op
        return result;
    }

    // 如果函数的返回值是一个结构体，并且超过了 16byte, caller 需要为该返回值申请足够的 stack 空间，并将 stack 地址放到 x8 寄存器中
    if (args_pos[0] == 1) {
        assert(call_result_type.kind == TYPE_STRUCT || call_result_type.kind == TYPE_ARR);

        assertf(call_result->assert_type == LIR_OPERAND_VAR, "call result must a var");
        assert(is_stack_ref_big_type(lir_operand_type(call_result)));

        lir_operand_t *result_reg_operand = operand_new(LIR_OPERAND_REG, x8);

        // linear call 的时候没有为 result 申请空间，此处进行空间申请, 为 call 申请了空间，并将空间地址放在 x8 寄存器, callee 会使用该寄存器
        linked_push(result, lir_stack_alloc(c, call_result_type, call_result));
        linked_push(result, lir_op_move(result_reg_operand, call_result));
    }


    // 返回值处理: 参数已经处理完毕，开始调用 call 指令。其中 call_result
    if (args_pos[0] == 1) {
        // callee 已经将数据写入到了 call_result(x8寄存器对应的栈空间中)，此时不需要显式的处理 call_result,
        assert(call_result_type.kind == TYPE_STRUCT || call_result_type.kind == TYPE_ARR);

        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, NULL, op->line, op->column));
        return result;
    }

    // 由于结构体小于等于 16byte, 所以可以通过寄存器传递，此时栈中没有返回值的空间，所以需要进行空间申请
    if (is_stack_ref_big_type(call_result_type)) {
        assert(call_result->assert_type == LIR_OPERAND_VAR);
        linked_push(result, lir_stack_alloc(c, call_result_type, call_result));
    }


    // struct 通过指针传递时不需要考虑 hfa 优化。下面才需要考虑 hfa 优化。
    uint8_t call_result_size = type_sizeof(call_result_type);
    if (call_result_type.kind == TYPE_STRUCT) {
        // 返回值通过 1 到两个寄存器进行返回, 需要记录在 output_regs 中保证 use def 链完整，方便后续的寄存器分配
        slice_t *output_regs = slice_new();
        // 从新计算 output_res
        if (args_pos[0] < 16) {
            // 通过通用寄存器传递, x0, x1
            slice_push(output_regs, x0);
            if (call_result_size > QWORD) {
                slice_push(output_regs, x1);
            }
        } else {
            assert(args_pos[0] < 32);
            int64_t reg_index = (args_pos[0] >> 1) - 8;

            // 结构体通过 float 指针传递，说明进行了 hfa 优化
            int32_t fsize = 0;
            int32_t n = arm64_hfa(call_result_type, &fsize); // 返回 hfa 数量
            assert(n > 0 && n <= 4);
            assert(fsize == 4 || fsize == 8);
            type_kind ftype_kind = TYPE_FLOAT32;
            if (fsize == 8) {
                ftype_kind = TYPE_FLOAT64;
            }
            int64_t struct_offset = 0;
            for (int j = 0; j < n; ++j) {
                struct_offset += fsize;
                slice_push(output_regs, reg_select(reg_index + j, ftype_kind));
            }
        }

        lir_operand_t *new_output = operand_new(LIR_OPERAND_REGS, output_regs);
        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, new_output, op->line, op->column));

        // 将返回值 mov 到 call_result 中

        if (args_pos[0] < 16) {
            // 通过通用寄存器传递, x0, x1
            lir_operand_t *lo_src_reg = operand_new(LIR_OPERAND_REG, x0);
            lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), call_result, 0);

            linked_push(result, lir_op_move(dst, lo_src_reg));

            if (call_result_size > QWORD) {
                lir_operand_t *hi_src_reg = operand_new(LIR_OPERAND_REG, x1);
                dst = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), call_result, QWORD);
                linked_push(result, lir_op_move(dst, hi_src_reg));
            }
        } else {
            assert(args_pos[0] < 32);
            int64_t reg_index = (args_pos[0] >> 1) - 8;

            // 结构体通过 float 指针传递，说明进行了 hfa 优化
            int32_t fsize = 0;
            int32_t n = arm64_hfa(call_result_type, &fsize); // 返回 hfa 数量
            assert(n > 0 && n <= 4);
            assert(fsize == 4 || fsize == 8);
            type_kind ftype_kind = TYPE_FLOAT32;
            if (fsize == 8) {
                ftype_kind = TYPE_FLOAT64;
            }

            int64_t struct_offset = 0;
            for (int j = 0; j < n; ++j) {
                struct_offset += fsize;

                lir_operand_t *src_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index + j, ftype_kind));
                lir_operand_t *dst_operand = indirect_addr_operand(c->module, type_kind_new(ftype_kind),
                                                                   call_result, struct_offset);

                linked_push(result, lir_op_move(dst_operand, src_operand));
            }
        }
    } else {
        lir_operand_t *src;
        // int or float
        if (args_pos[0] < 16) {
            // int
            src = operand_new(LIR_OPERAND_REG, reg_select(x0->index, call_result_type.kind));
        } else {
            assert(args_pos[0] < 32);
            src = operand_new(LIR_OPERAND_REG, reg_select(v0->index, call_result_type.kind));
        }

        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, src, op->line, op->column));

        linked_push(result, lir_op_move(call_result, src));
    }

    return result;
}

linked_t *arm64_lower_fn_end(closure_t *c, lir_op_t *op) {
    assert(c != NULL);
    assert(op != NULL);

    linked_t *result = linked_new();
    lir_operand_t *return_operand = op->first;
    if (!return_operand) {
        linked_push(result, op);
        return result;
    }

    // return_operand 的相关空间由 caller 提供
    type_t return_type = lir_operand_type(return_operand);
    uint8_t return_size = type_sizeof(return_type);

    if (return_type.kind == TYPE_STRUCT) {
        int32_t fsize = 0;
        int32_t hfa_count = arm64_hfa(return_type, &fsize);

        if (hfa_count > 0 && hfa_count <= 4) {
            // HFA 优化情况
            type_kind ftype_kind = (fsize == 4) ? TYPE_FLOAT32 : TYPE_FLOAT64;
            for (int i = 0; i < hfa_count; i++) {
                lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(ftype_kind), return_operand,
                                                           i * fsize);

                lir_operand_t *dst = operand_new(LIR_OPERAND_REG, reg_select(v0->index + i, ftype_kind));
                linked_push(result, lir_op_move(dst, src));
            }
        } else if (return_size <= 16) {
            // 小于等于 16 字节的结构体，使用 x0 和 x1 返回
            lir_operand_t *src_lo = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), return_operand, 0);
            lir_operand_t *dst_lo = operand_new(LIR_OPERAND_REG, x0);
            linked_push(result, lir_op_move(dst_lo, src_lo));

            if (return_size > 8) {
                lir_operand_t *src_hi = indirect_addr_operand(c->module, type_kind_new(TYPE_UINT64), return_operand, 8);
                lir_operand_t *dst_hi = operand_new(LIR_OPERAND_REG, x1);
                linked_push(result, lir_op_move(dst_hi, src_hi));
            }
        } else {
            // 大于 16 字节的结构体，通过内存返回，linear_return 时已经将相关数据存储在了 return_operand 中, x8 寄存器中存储的指针被传递给了 return_operand
            // 这里什么都不需要做
        }
    } else {
        // 非结构体类型
        lir_operand_t *dst;
        if (is_float(return_type.kind)) {
            dst = operand_new(LIR_OPERAND_REG, reg_select(v0->index, return_type.kind));
        } else {
            dst = operand_new(LIR_OPERAND_REG, reg_select(x0->index, return_type.kind));
        }
        linked_push(result, lir_op_move(dst, return_operand));
    }

    op->first = NULL;
    linked_push(result, op);
    return result;
}
