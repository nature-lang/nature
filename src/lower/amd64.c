#include "amd64.h"

#include "amd64_abi.h"

static lir_operand_t *amd64_convert_first_to_temp(closure_t *c, linked_t *list, lir_operand_t *first) {
    lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(first));

    linked_push(list, lir_op_move(temp, first));
    return lir_reset_operand(temp, first->pos);
}


static linked_t *amd64_lower_neg(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    //    assert(op->output->assert_type == LIR_OPERAND_VAR);

    lir_operand_t *old_output = NULL;
    if (op->output->assert_type != LIR_OPERAND_VAR) {
        // output 替换
        lir_operand_t *new_output = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
        old_output = op->output;
        op->output = lir_reset_operand(new_output, op->output->pos);
    }

    type_kind kind = operand_type_kind(op->output);
    assert(is_number(kind));
    if (is_integer_or_anyptr(kind)) {
        linked_push(list, op);

        if (old_output) {
            linked_push(list, lir_op_move(old_output, op->output));
        }

        return list;
    }

    linked_push(list, lir_op_move(op->output, op->first));

    // xor float 需要覆盖满整个 xmm 寄存器(128bit), 所以这里直接用 symbol 最多只能有 f64 = 64bit
    // 这里用 xmm1 进行一个中转
    lir_operand_t *xmm_operand = amd64_select_return_reg(op->output);
    if (kind == TYPE_FLOAT64) {
        linked_push(list, lir_op_move(xmm_operand, symbol_var_operand(F64_NEG_MASK_IDENT, kind)));
    } else {
        linked_push(list, lir_op_move(xmm_operand, symbol_var_operand(F32_NEG_MASK_IDENT, kind)));
    }

    linked_push(list, lir_op_new(LIR_OPCODE_XOR, op->output, xmm_operand, op->output));

    if (old_output) {
        linked_push(list, lir_op_move(old_output, op->output));
    }


    return list;
}

/**
 * amd64 由于不支持直接操作浮点型和字符串, 所以将其作为全局变量直接注册到 closure asm_symbols 中
 * 并添加 lea 指令对值进行加载
 * 所以在 string 和 float 类型进行特殊处理，可以更容易在 native trans 时进行操作
 * @param c
 * @param block
 * @param node
 */
static linked_t *amd64_lower_imm(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    // imm 提取
    slice_t *imm_operands = extract_op_operands(op, FLAG(LIR_OPERAND_IMM), 0, false);
    for (int i = 0; i < imm_operands->count; ++i) {
        lir_operand_t *imm_operand = imm_operands->take[i];
        lir_imm_t *imm = imm_operand->value;

        if (imm->kind == TYPE_RAW_STRING || is_float(imm->kind)) {
            char *unique_name = var_unique_ident(c->module, TEMP_VAR_IDENT);
            asm_global_symbol_t *symbol = NEW(asm_global_symbol_t);
            symbol->name = unique_name;
            if (imm->kind == TYPE_RAW_STRING) {
                symbol->size = strlen(imm->string_value) + 1;
                symbol->value = (uint8_t *) imm->string_value;
            } else if (imm->kind == TYPE_FLOAT64) {
                symbol->size = type_kind_sizeof(imm->kind);
                symbol->value = (uint8_t *) &imm->f64_value;
            } else if (imm->kind == TYPE_FLOAT32) {
                symbol->size = type_kind_sizeof(imm->kind);
                symbol->value = (uint8_t *) &imm->f32_value;
            } else {
                assertf(false, "not support type %s", type_kind_str[imm->kind]);
            }

            slice_push(c->asm_symbols, symbol);
            lir_symbol_var_t *symbol_var = NEW(lir_symbol_var_t);
            symbol_var->kind = imm->kind;
            symbol_var->ident = unique_name;

            if (imm->kind == TYPE_RAW_STRING) {
                // raw_string 本身就是指针类型, 首次加载时需要通过 lea 将 .data 到 raw_string 的起始地址加载到 var_operand
                lir_operand_t *var_operand = temp_var_operand_with_alloc(c->module, type_kind_new(TYPE_RAW_STRING));
                lir_op_t *temp_ref = lir_op_lea(var_operand, operand_new(LIR_OPERAND_SYMBOL_VAR, symbol_var));
                linked_push(list, temp_ref);

                lir_operand_t *temp_operand = lir_reset_operand(var_operand, imm_operand->pos);
                imm_operand->assert_type = temp_operand->assert_type;
                imm_operand->value = temp_operand->value;
            } else {
                // float 直接修改地址，通过 rip 寻址即可, symbol value 已经添加到全局符号表中
                imm_operand->assert_type = LIR_OPERAND_SYMBOL_VAR;
                imm_operand->value = symbol_var;
            }

        } else if (is_qword_int(imm->kind)) {
            if (op->code != LIR_OPCODE_MOVE || op->output->assert_type != LIR_OPERAND_VAR) {
                // 大数值必须通过 reg 转化,
                type_kind kind = operand_type_kind(imm_operand);
                lir_operand_t *temp = temp_var_operand_with_alloc(c->module, type_kind_new(kind));

                linked_push(list, lir_op_move(temp, imm_operand));
                temp = lir_reset_operand(temp, imm_operand->pos);
                imm_operand->assert_type = temp->assert_type;
                imm_operand->value = temp->value;
            }
        }
    }

    return list;
}

///**
// * env closure 特有 call 指令
// * @param c
// * @param op
// * @return
// */
//linked_t *amd64_lower_env_closure(closure_t *c, lir_op_t *op) {
//    linked_t *list = linked_new();
//    assert(op->first->assert_type == LIR_OPERAND_CLOSURE_VARS);
//
//    slice_t *closure_vars = op->first->value;
//    assert(closure_vars->count > 0);
//    for (int i = 0; i < closure_vars->count; ++i) {
//        lir_var_t *var = closure_vars->take[i];
//        int64_t stack_slot = var_stack_slot(c, var);
//        assert(stack_slot < 0);
//        lir_stack_t *stack = NEW(lir_stack_t);
//        stack->slot = stack_slot;
//        stack->size = type_kind_sizeof(var->type.kind);
//        // rdi param
//        lir_operand_t *stack_operand = operand_new(LIR_OPERAND_STACK, stack);
//        if (is_defer_alloc_type(var->type)) {
//            // stack_operand 中保存的就是一个栈地址，此时不需要进行 lea, 只是直接进行 mov 取值
//            linked_push(list, lir_op_move(operand_new(LIR_OPERAND_REG, rdi), stack_operand));
//        } else {
//            linked_push(list, lir_op_lea(operand_new(LIR_OPERAND_REG, rdi), stack_operand));
//        }
//        // rsi param
//        linked_push(list, lir_op_move(operand_new(LIR_OPERAND_REG, rsi), int_operand(ct_find_rtype_hash(var->type))));
//
//        linked_push(list, lir_op_new(LIR_OPCODE_RT_CALL, lir_label_operand(RT_CALL_ENV_CLOSURE, false),
//                                     operand_new(LIR_OPERAND_ARGS, slice_new()), NULL));
//    }
//
//    return list;
//}

/**
 * 三元组中  add imm, rax -> rax 是完全合法都指令，但是需要转换成二元组时
 * 却无法简单都通过 mov first -> result 来操作，因为此时 result = second, 所以 first 会覆盖掉 second 都值
 * @param c
 * @param op
 * @return
 */
static linked_t *amd64_lower_ternary(closure_t *c, lir_op_t *op) {
    assert(op->first && op->second && op->output);

    linked_t *list = linked_new();
    // 通过一个临时 var, 让 first = output = reg, 从而将三元转换成二元表达式
    type_kind kind = operand_type_kind(op->output);
    lir_operand_t *temp = temp_var_operand_with_alloc(c->module, type_kind_new(kind));

    linked_push(list, lir_op_move(temp, op->first));
    linked_push(list, lir_op_new(op->code, temp, op->second, temp));
    linked_push(list, lir_op_move(op->output, temp));

    return list;
}

static linked_t *amd64_lower_shift(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    // second to cl/rcx
    lir_operand_t *fit_cx_operand = lir_reg_operand(cl->index, operand_type_kind(op->second));
    linked_push(list, lir_op_move(fit_cx_operand, op->second));

    type_kind kind = operand_type_kind(op->output);
    lir_operand_t *temp = temp_var_operand_with_alloc(c->module, type_kind_new(kind));
    linked_push(list, lir_op_move(temp, op->first));

    // 这里相当于做了一次基于寄存器的类型转换了
    lir_operand_t *cl_operand = lir_reg_operand(cl->index, TYPE_UINT8);
    // sar/sal
    linked_push(list, lir_op_new(op->code, temp, cl_operand, temp));
    linked_push(list, lir_op_move(op->output, temp));

    return list;
}

static linked_t *amd64_lower_mul(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    lir_operand_t *ax_operand = lir_reg_operand(rax->index, operand_type_kind(op->output));
    lir_operand_t *dx_operand = lir_reg_operand(rdx->index, operand_type_kind(op->output));

    // second cannot imm?
    if (op->second->assert_type != LIR_OPERAND_VAR) {
        op->second = amd64_convert_first_to_temp(c, list, op->second);
    }

    // mov first -> rax
    linked_push(list, lir_op_move(ax_operand, op->first));

    lir_opcode_t op_code = op->code;
    lir_operand_t *result_operand = lir_regs_operand(2, ax_operand->value, dx_operand->value);


    // imul rax, v2 -> rax+rdx
    linked_push(list, lir_op_new(op_code, ax_operand, op->second, result_operand));
    linked_push(list, lir_op_move(op->output, ax_operand)); // 暂时不处理 rdx, 只支持 64

    return list;
}

static linked_t *amd64_lower_factor(closure_t *c, lir_op_t *op) {
    if (op->code == LIR_OPCODE_MUL) {
        return amd64_lower_mul(c, op);
    }

    linked_t *list = linked_new();

    lir_operand_t *ax_operand = lir_reg_operand(rax->index, operand_type_kind(op->output));
    lir_operand_t *dx_operand = lir_reg_operand(rdx->index, operand_type_kind(op->output));

    // second cannot imm?
    if (op->second->assert_type != LIR_OPERAND_VAR) {
        op->second = amd64_convert_first_to_temp(c, list, op->second);
    }

    // mov first -> rax
    linked_push(list, lir_op_move(ax_operand, op->first));

    lir_opcode_t op_code = op->code;
    lir_operand_t *result_operand = ax_operand;
    if (op->code == LIR_OPCODE_UREM) {
        op_code = LIR_OPCODE_UDIV; // rem 也是基于 div 计算得到的
        result_operand = dx_operand; // 余数固定寄存器
    }
    if (op->code == LIR_OPCODE_SREM) {
        op_code = LIR_OPCODE_SDIV;
        result_operand = dx_operand; // 余数固定寄存器
    }

    assert(op_code == LIR_OPCODE_UDIV || op_code == LIR_OPCODE_SDIV);

    // 64位操作系统中寄存器大小当然只有64位，因此，idiv使用rdx:rax作为被除数
    // 即rdx中的值作为高64位、rax中的值作为低64位
    // 格式：idiv src，结果存储在rax中
    // 因此，在使用idiv进行计算时，rdx 中不得为随机值，否则会发生浮点异常。
    linked_push(list, lir_op_new(LIR_OPCODE_CLR, NULL, NULL, dx_operand));

    // mul 使用 rax:rdx 两个寄存器，需要调整 result_operand

    // [div|mul|rem] rax, v2 -> rax+rdx
    linked_push(list, lir_op_new(op_code, ax_operand, op->second, result_operand));
    linked_push(list, lir_op_move(op->output, result_operand));

    return list;
}

static linked_t *amd64_lower_safepoint(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    lir_operand_t *result_operand;
    if (BUILD_OS == OS_LINUX) {
        result_operand = operand_new(LIR_OPERAND_REG, rax);
    } else {
        result_operand = lir_regs_operand(2, rax, rdi);
    }
    // 预留 rax 寄存器存储 call 的结果
    //    lir_operand_t *result_reg = lir_reg_operand(rax->index, TYPE_ANYPTR);

    // 预留 rdi 用于参数(rdi 在 use 会导致 reg 的 use-def 异常), 只保留
    //    lir_operand_t *first_reg = lir_reg_operand(rdi->index, TYPE_ANYPTR);


    // 增加 label continue
    linked_push(list, lir_op_new(op->code, NULL, NULL, result_operand));

    return list;
}

static void amd64_lower_block(closure_t *c, basic_block_t *block) {
    linked_t *operations = linked_new();
    LINKED_FOR(block->operations) {
        lir_op_t *op = LINKED_VALUE();

        linked_concat(operations, amd64_lower_imm(c, op));

        if (lir_op_call(op) && op->second->value != NULL) {
            linked_concat(operations, amd64_lower_call(c, op));
            continue;
        }
        if (op->code == LIR_OPCODE_NEG) {
            linked_concat(operations, amd64_lower_neg(c, op));
            continue;
        }
        if (op->code == LIR_OPCODE_FN_BEGIN) {
            linked_concat(operations, amd64_lower_fn_begin(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_FN_END) {
            linked_concat(operations, amd64_lower_fn_end(c, op));
            continue;
        }

        // float 不需要特别处理, 其不会特殊占用寄存器
        if (lir_op_factor(op) && is_integer_or_anyptr(operand_type_kind(op->output))) {
            // inter 类型的 mul 和 div 需要转换成 amd64 单操作数兼容操作
            linked_concat(operations, amd64_lower_factor(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_USHL || op->code == LIR_OPCODE_USHR || op->code == LIR_OPCODE_SSHR) {
            linked_concat(operations, amd64_lower_shift(c, op));
            continue;
        }

        if (lir_op_contain_cmp(op) && op->first->assert_type != LIR_OPERAND_VAR) {
            op->first = amd64_convert_first_to_temp(c, operations, op->first);
            linked_push(operations, op);
            continue;
        }

        if (op->code == LIR_OPCODE_SAFEPOINT) {
            linked_concat(operations, amd64_lower_safepoint(c, op));
            continue;
        }

        // lea symbol_label -> var 等都是允许的，主要是应对 imm int
        if (op->code == LIR_OPCODE_LEA && op->first->assert_type == LIR_OPERAND_IMM) {
            op->first = amd64_convert_first_to_temp(c, operations, op->first);
            linked_push(operations, op);
            continue;
        }

        if (op->code == LIR_OPCODE_LEA && !lir_can_lea(op)) {
            // lea first output
            // ---- change to
            // lea first -> temp_var
            // mov temp_var -> output
            lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
            linked_push(operations, lir_op_lea(temp, op->first));
            linked_push(operations, lir_op_move(op->output, temp));

            continue;
        }

        if (lir_op_like_move(op) && !lir_can_mov(op)) {
            op->first = amd64_convert_first_to_temp(c, operations, op->first);
            linked_push(operations, op);
            continue;
        }

        // 所有都三元运算都是不兼容 amd64 的，所以这里尽可能的进行三元转换为二元的处理
        if (lir_op_ternary(op)) {
            linked_concat(operations, amd64_lower_ternary(c, op));
            continue;
        }

        linked_push(operations, op);
    }

    block->operations = operations;
}

void amd64_lower(closure_t *c) {
    // 按基本块遍历所有指令
    SLICE_FOR(c->blocks) {
        basic_block_t *block = SLICE_VALUE(c->blocks);
        amd64_lower_block(c, block);

        // 设置 block 的首尾 op
        lir_set_quick_op(block);
    }
}
