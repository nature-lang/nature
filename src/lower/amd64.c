#include "amd64.h"
#include "src/cross.h"
#include "src/register/amd64.h"

static lir_operand_t *amd64_convert_to_var(closure_t *c, linked_t *list, lir_operand_t *operand) {
    type_kind kind = operand_type_kind(operand);
    lir_operand_t *temp = temp_var_operand(c->module, type_basic_new(kind));

    linked_push(list, lir_op_move(temp, operand));
    return lir_reset_operand(temp, operand->pos);
}

static lir_operand_t *select_return_reg(lir_operand_t *operand) {
    type_kind kind = operand_type_kind(operand);
    if (kind == TYPE_FLOAT || kind == TYPE_FLOAT64) {
        return operand_new(LIR_OPERAND_REG, xmm0s64);
    }
    if (kind == TYPE_FLOAT32) {
        return operand_new(LIR_OPERAND_REG, xmm0s32);
    }

    return reg_operand(rax->index, kind);
}

/**
 * call actual params handle
 * mov var -> rdi
 * mov var -> rax
 * @param c
 * @param args
 * @return
 */
static linked_t *amd64_args_lower(closure_t *c, slice_t *args) {
    linked_t *operations = linked_new();
    linked_t *push_operations = linked_new();
    int push_length = 0;
    uint8_t used[2] = {0};
    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *param_operand = args->take[i];
        type_kind type_kind = operand_type_kind(param_operand);
        reg_t *reg = amd64_fn_param_next_reg(used, type_kind);
        if (reg) {
            // 再全尺寸模式下清空 reg 避免因为 reg 空间占用导致的异常问题
            if (reg->size < QWORD && is_integer(type_kind)) {
                linked_push(operations, lir_op_new(LIR_OPCODE_CLR, NULL, NULL,
                                                   operand_new(LIR_OPERAND_REG, covert_alloc_reg(reg))));
            }

            // TODO 使用 movzx 或者 movsx 可以做尺寸不匹配到 mov, 就不用做上面到 CLR 了
            lir_op_t *op = lir_op_move(operand_new(LIR_OPERAND_REG, reg), param_operand);

            linked_push(operations, op);
        } else {
            // 参数在栈空间中总是 8byte 使用,所以给定任意参数 n, 在不知道其 size 的情况行也能取出来
            // 不需要 move, 直接走 push 指令即可, 这里虽然操作了 rsp，但是 rbp 是没有变化的
            // 不过 call 之前需要保证 rsp 16 byte 对齐
            lir_op_t *push_op = lir_op_new(LIR_OPCODE_PUSH, param_operand, NULL, NULL);
            linked_push(push_operations, push_op);
            push_length += QWORD;
        }
    }
    // 由于使用了 push 指令操作了堆栈，可能导致堆栈不对齐，所以需要操作一下堆栈对齐
    uint64_t diff_length = align(push_length, 16) - push_length;

    // sub rsp - 1 = rsp
    // 先 sub 再 push, 保证 rsp 16 byte 对齐
    if (diff_length > 0) {
        lir_op_t *binary_op = lir_op_new(LIR_OPCODE_SUB,
                                         operand_new(LIR_OPERAND_REG, rsp),
                                         int_operand(diff_length),
                                         operand_new(LIR_OPERAND_REG, rsp));
        linked_push(push_operations, binary_op);
    }

    // 倒序 push operations 追加到 operations 中
    linked_node *current = linked_last(push_operations);
    while (current && current->value) {
        linked_push(operations, current->value);
        current = current->prev;
    }

    return operations;
}

/**
 * 寄存器选择进行了 fit 匹配
 * @param c
 * @param formals
 * @return
 */
static linked_t *amd64_formals_lower(closure_t *c, slice_t *formals) {
    linked_t *operations = linked_new();
    uint8_t used[2] = {0};
    // 16byte 起点, 因为 call 和 push rbp 占用了16 byte空间, 当参数寄存器用完的时候，就会使用 stack offset 了
    int16_t stack_param_slot = 16; // ret addr + push rsp
    for (int _i = 0; _i < (formals)->count; ++_i) {
        lir_var_t *var = formals->take[_i];
        lir_operand_t *source = NULL;
        reg_t *reg = amd64_fn_param_next_reg(used, var->type.kind);
        if (reg) {
            source = operand_new(LIR_OPERAND_REG, reg);

            if (c->fn_runtime_operand != NULL && _i == formals->count - 1) {
                c->fn_runtime_reg = reg->index;
            }
        } else {
            lir_stack_t *stack = NEW(lir_stack_t);
            // caller 虽然使用了 pushq 指令进栈，但是实际上并不需要使用这么大的空间,
            stack->size = type_kind_sizeof(var->type.kind);
            stack->slot = stack_param_slot; // caller push 入栈的参数的具体位置
            if (c->fn_runtime_operand != NULL && _i == formals->count - 1) {
                c->fn_runtime_stack = stack->slot;
            }

            // 如果是 c 的话会有 16byte,但 nature 最大也就 8byte 了
            // 固定 QWORD(caller float 也是 8 byte，只是不直接使用 push)
            stack_param_slot += QWORD; // 固定 8 bit， 不过 8byte 会造成 stack_slot align exception

            source = operand_new(LIR_OPERAND_STACK, stack);
        }

        lir_op_t *op = lir_op_move(operand_new(LIR_OPERAND_VAR, var), source);
        linked_push(operations, op);
    }

    return operations;
}


static linked_t *amd64_lower_neg(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    // var 才能分配 reg, 正常来说肯定是，如果不是就需要做 convert
    assert(op->output->assert_type == LIR_OPERAND_VAR);

    type_kind kind = operand_type_kind(op->output);
    assert(is_number(kind));
    if (is_integer(kind)) {
        linked_push(list, op);
        return list;
    }
    // 副店形操作

    linked_push(list, lir_op_move(op->output, op->first));
    // xor float 需要覆盖满整个 xmm 寄存器(128bit), 所以这里直接用 symbol 最多只能有 f64 = 64bit
    // 这里用 xmm1 进行一个中转
    lir_operand_t *xmm_operand = select_return_reg(op->output);
    linked_push(list, lir_op_move(xmm_operand, symbol_var_operand(FLOAT_NEG_MASK_IDENT, kind)));


    linked_push(list, lir_op_new(LIR_OPCODE_XOR,
                                 op->output,
                                 xmm_operand,
                                 op->output));
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
                lir_operand_t *var_operand = temp_var_operand(c->module, type_basic_new(TYPE_RAW_STRING));
                lir_op_t *temp_ref = lir_op_lea(var_operand, operand_new(LIR_OPERAND_SYMBOL_VAR, symbol_var));
                linked_push(list, temp_ref);

                lir_operand_t *temp_operand = lir_reset_operand(var_operand, imm_operand->pos);
                imm_operand->assert_type = temp_operand->assert_type;
                imm_operand->value = temp_operand->value;
            } else {
                imm_operand->assert_type = LIR_OPERAND_SYMBOL_VAR;
                imm_operand->value = symbol_var;
            }

        } else if (is_qword_int(imm->kind)) {
            // 大数值必须通过 reg 转化
            type_kind kind = operand_type_kind(imm_operand);
            lir_operand_t *temp = temp_var_operand(c->module, type_basic_new(kind));

            linked_push(list, lir_op_move(temp, imm_operand));
            temp = lir_reset_operand(temp, imm_operand->pos);
            imm_operand->assert_type = temp->assert_type;
            imm_operand->value = temp->value;
        }
    }

    return list;
}

/**
 * env closure 特有 call 指令
 * @param c
 * @param op
 * @return
 */
linked_t *amd64_lower_env_closure(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    assert(op->first->assert_type == LIR_OPERAND_CLOSURE_VARS);

    slice_t *closure_vars = op->first->value;
    assert(closure_vars->count > 0);
    for (int i = 0; i < closure_vars->count; ++i) {
        lir_var_t *var = closure_vars->take[i];
        int64_t stack_slot = var_stack_slot(c, var);
        assert(stack_slot < 0);
        lir_stack_t *stack = NEW(lir_stack_t);
        stack->slot = stack_slot;
        stack->size = type_kind_sizeof(var->type.kind);
        lir_operand_t *stack_operand = operand_new(LIR_OPERAND_STACK, stack);
        linked_push(list, lir_op_lea(operand_new(LIR_OPERAND_REG, rdi), stack_operand));
        linked_push(list, rt_call(RT_CALL_ENV_CLOSURE, NULL, 0));
    }

    return list;
}

static linked_t *amd64_lower_call(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    // lower call actual params
    linked_t *temps = amd64_args_lower(c, op->second->value);
    linked_concat(list, temps);
    op->second->value = slice_new();

    if (op->output == NULL) {
        linked_push(list, op);
    } else {
        lir_operand_t *reg_operand = select_return_reg(op->output);
        linked_push(list, lir_op_new(op->code, op->first, op->second, reg_operand));
        linked_push(list, lir_op_move(op->output, reg_operand));
    }

    return list;
}

static linked_t *amd64_lower_fn_begin(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    linked_push(list, op);

    // fn begin
    // mov rsi -> formal param 1
    // mov rdi -> formal param 2
    // ....
    linked_t *temps = amd64_formals_lower(c, op->output->value);
    linked_node *current = linked_last(temps);
    while (current && current->value != NULL) {
        linked_push(list, current->value);
        current = current->prev;
    }
    op->output->value = slice_new();

    return list;
}

static linked_t *amd64_lower_fn_end(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    // 1.1 return 指令需要将返回值放到 rax 中
    lir_operand_t *reg_operand = select_return_reg(op->first);
    linked_push(list, lir_op_move(reg_operand, op->first));
    linked_push(list, lir_op_new(op->code, reg_operand, NULL, NULL));
    return list;
}

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
    // 通过一个临时 var, 领 first = output = reg, 从而将三元转换成二元表达式
    type_kind kind = operand_type_kind(op->output);
    lir_operand_t *temp = temp_var_operand(c->module, type_basic_new(kind));

    linked_push(list, lir_op_move(temp, op->first));
    linked_push(list, lir_op_new(op->code, temp, op->second, temp));
    linked_push(list, lir_op_move(op->output, temp));

    return list;
}

static linked_t *amd64_lower_shift(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    // second to cl/rcx
    lir_operand_t *fit_cx_operand = reg_operand(cl->index, operand_type_kind(op->second));
    linked_push(list, lir_op_move(fit_cx_operand, op->second));

    type_kind kind = operand_type_kind(op->output);
    lir_operand_t *temp = temp_var_operand(c->module, type_basic_new(kind));
    linked_push(list, lir_op_move(temp, op->first));

    // 这里相当于做了一次基于寄存器的类型转换了
    lir_operand_t *cl_operand = reg_operand(cl->index, TYPE_UINT8);
    // sar/sal
    linked_push(list, lir_op_new(op->code, temp, cl_operand, temp));
    linked_push(list, lir_op_move(op->output, temp));

    return list;
}

static linked_t *amd64_lower_factor(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    lir_operand_t *ax_operand = reg_operand(rax->index, operand_type_kind(op->output));
    lir_operand_t *dx_operand = reg_operand(rdx->index, operand_type_kind(op->output));

    // second cannot imm?
    if (op->second->assert_type != LIR_OPERAND_VAR) {
        op->second = amd64_convert_to_var(c, list, op->second);
    }

    // mov first -> rax
    linked_push(list, lir_op_move(ax_operand, op->first));

    lir_opcode_t op_code = op->code;
    lir_operand_t *result_operand = ax_operand;
    if (op->code == LIR_OPCODE_REM) {
        op_code = LIR_OPCODE_DIV; // rem 也是基于 div 计算得到的
        result_operand = dx_operand; // 余数固定寄存器
    }

    // 64位操作系统中寄存器大小当然只有64位，因此，idiv使用rdx:rax作为被除数
    // 即rdx中的值作为高64位、rax中的值作为低64位
    // 格式：idiv src，结果存储在rax中
    // 因此，在使用idiv进行计算时，rdx 中不得为随机值，否则会发生浮点异常。
    if (op_code == LIR_OPCODE_DIV) {
        // TODO 如果寄存器分配识别异常可以考虑 first = dx_operand, 让 fixed interval 的固定生命周期完善
        linked_push(list, lir_op_new(LIR_OPCODE_CLR, NULL, NULL, dx_operand));
    }

    // [div|mul|rem] rax, v2 -> rax
    linked_push(list, lir_op_new(op_code, ax_operand, op->second, result_operand));
    linked_push(list, lir_op_move(op->output, result_operand));

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
        if (op->code == LIR_OPCODE_FN_END && op->first != NULL) {
            linked_concat(operations, amd64_lower_fn_end(c, op));
            continue;
        }
        if (lir_op_factor(op) && is_integer(operand_type_kind(op->output))) {
            // inter 类型的 mul 和 div 需要转换成 amd64 单操作数兼容操作
            linked_concat(operations, amd64_lower_factor(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_SHL || op->code == LIR_OPCODE_SHR) {
            linked_concat(operations, amd64_lower_shift(c, op));
            continue;
        }

        if (lir_op_contain_cmp(op) && op->first->assert_type != LIR_OPERAND_VAR) {
            op->first = amd64_convert_to_var(c, operations, op->first);
            linked_push(operations, op);
            continue;
        }

        // lea symbol_label -> var 等都是允许的，主要是应对 imm int
        if (op->code == LIR_OPCODE_LEA && op->first->assert_type == LIR_OPERAND_IMM) {
            op->first = amd64_convert_to_var(c, operations, op->first);
            linked_push(operations, op);
            continue;
        }

        // 所有都三元运算都是不兼容 amd64 的，所以这里尽可能的进行三元转换为二元的处理
        if (is_ternary(op)) {
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
