#include "amd64.h"
#include "amd64_abi.h"
#include "lower.h"
#include "src/lir.h"
#include "src/types.h"

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

    // op->second 是 -0.0 立即数，已在 amd64_lower_block 中被 lower_imm 转换为 var
    // -0.0 的位表示就是符号位掩码 (0x80000000 for f32, 0x8000000000000000 for f64)
    assert(op->second && op->second->assert_type == LIR_OPERAND_VAR);
    linked_push(list, lir_op_new(LIR_OPCODE_XOR, op->output, op->second, op->output));

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
static linked_t *amd64_lower_imm(closure_t *c, lir_op_t *op, linked_t *symbol_operations) {
    linked_t *list = linked_new();

    // imm 提取
    slice_t *imm_operands = extract_op_operands(op, FLAG(LIR_OPERAND_IMM), 0, false);
    for (int i = 0; i < imm_operands->count; ++i) {
        lir_operand_t *imm_operand = imm_operands->take[i];
        lir_imm_t *imm = imm_operand->value;

        if (imm->kind == TYPE_RAW_STRING || is_float(imm->kind)) {

            lower_imm_symbol(c, imm_operand, list, symbol_operations);

        } else if (is_qword_int(imm->kind)) {
            // mov r64,imm64 转换成 mov rm64,imm32
            if (imm->int_value > INT32_MAX || imm->int_value < INT32_MIN) {
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
    }

    return list;
}

/**
 * AMD64 对于整数类型的 ADD/SUB/AND/OR/XOR 支持 imm 作为第二操作数
 * 所以对于这些操作，不需要将 imm 转换为临时变量
 */
static bool amd64_ternary_support_imm(lir_op_t *op) {
    if (op->second->assert_type != LIR_OPERAND_IMM) {
        return false;
    }
    if (!is_integer_or_anyptr(operand_type_kind(op->output))) {
        return false;
    }
    // ADD, SUB, AND, OR, XOR 支持 imm 作为第二操作数
    return op->code == LIR_OPCODE_ADD ||
           op->code == LIR_OPCODE_SUB ||
           op->code == LIR_OPCODE_AND ||
           op->code == LIR_OPCODE_OR ||
           op->code == LIR_OPCODE_XOR;
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
    // 通过一个临时 var, 让 first = output = reg, 从而将三元转换成二元表达式
    type_t t = lir_operand_type(op->output);
    lir_operand_t *temp = temp_var_operand(c->module, t);

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
    lir_operand_t *temp = temp_var_operand(c->module, type_kind_new(kind));
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
    type_kind output_kind = operand_type_kind(op->output);

    // byte 类型使用单操作数 mul (需要 rax)
    if (output_kind == TYPE_UINT8 || output_kind == TYPE_INT8) {
        lir_operand_t *ax_operand = lir_reg_operand(rax->index, output_kind);

        // second cannot be imm for single-operand mul
        if (op->second->assert_type != LIR_OPERAND_VAR) {
            op->second = amd64_convert_first_to_temp(c, list, op->second);
        }

        // mov first -> al
        linked_push(list, lir_op_move(ax_operand, op->first));

        // mul al, second -> al (only care about low bits)
        linked_push(list, lir_op_new(op->code, ax_operand, op->second, ax_operand));
        linked_push(list, lir_op_move(op->output, ax_operand));

        return list;
    }

    // byte 以上类型使用 imul 策略
    // imul 支持两种形式:
    // 1. imul r, r/m (two-operand): r = r * r/m
    // 2. imul r, r/m, imm (three-operand): r = r/m * imm

    // 三操作数形式: first 或 second 有一个是 imm
    // imul 三操作数支持 first != output，无需额外 mov
    if (op->first->assert_type == LIR_OPERAND_IMM || op->second->assert_type == LIR_OPERAND_IMM) {
        // 确保 imm 在 second 位置
        if (op->first->assert_type == LIR_OPERAND_IMM) {
            lir_operand_t *temp = op->first;
            op->first = lir_reset_operand(op->second, LIR_FLAG_FIRST);
            op->second = lir_reset_operand(temp, LIR_FLAG_SECOND);
        }
        // 如果 first 仍然是 imm (两个都是 imm 的情况)，需要转换为 temp
        if (op->first->assert_type == LIR_OPERAND_IMM) {
            op->first = amd64_convert_first_to_temp(c, list, op->first);
        }

        // 三操作数形式 imul r, r/m, imm 要求 output 必须是寄存器
        lir_operand_t *old_output = NULL;
        if (op->output->assert_type != LIR_OPERAND_VAR) {
            lir_operand_t *new_output = temp_var_operand(c->module, lir_operand_type(op->output));
            old_output = op->output;
            op->output = lir_reset_operand(new_output, op->output->pos);
        }

        // 直接使用三操作数形式 imul output, first, second(imm)
        linked_push(list, op);

        if (old_output) {
            linked_push(list, lir_op_move(old_output, op->output));
        }

        return list;
    }

    // 两个操作数都是 var: 使用二操作数形式 imul r, r/m
    // 通过 amd64_lower_ternary 实现: mov first -> temp; imul temp, second -> temp; mov temp -> output
    return amd64_lower_ternary(c, op);
}

static linked_t *amd64_lower_factor(closure_t *c, lir_op_t *op) {
    if (op->code == LIR_OPCODE_MUL) {
        return amd64_lower_mul(c, op);
    }

    linked_t *list = linked_new();

    type_kind output_kind = operand_type_kind(op->output);

    // second cannot imm?
    if (op->second->assert_type != LIR_OPERAND_VAR) {
        op->second = amd64_convert_first_to_temp(c, list, op->second);
    }

    lir_opcode_t op_code = op->code;
    if (op->code == LIR_OPCODE_UREM) {
        op_code = LIR_OPCODE_UDIV; // rem 也是基于 div 计算得到的
    }
    if (op->code == LIR_OPCODE_SREM) {
        op_code = LIR_OPCODE_SDIV;
    }

    assert(op_code == LIR_OPCODE_UDIV || op_code == LIR_OPCODE_SDIV);

    // 根据操作数类型选择合适的寄存器处理方式
    if (output_kind == TYPE_UINT8 || output_kind == TYPE_INT8) {
        // 8位除法特殊处理
        // 被除数需要在AX(16位)，商在AL，余数在AH

        // 清零AX寄存器
        linked_push(list, lir_op_new(LIR_OPCODE_CLR, NULL, NULL, operand_new(LIR_OPERAND_REG, ax)));

        // 将被除数加载到AL
        lir_operand_t *al_operand = operand_new(LIR_OPERAND_REG, al);
        lir_operand_t *ah_operand = operand_new(LIR_OPERAND_REG, ah);
        linked_push(list, lir_op_move(al_operand, op->first));

        lir_operand_t *dividend_operand = lir_regs_operand(2, al_operand->value, ah_operand->value);

        // 执行8位除法，被除数在AX，除数是8位操作数
        linked_push(list, lir_op_new(op_code, dividend_operand, op->second, al_operand));

        // 根据操作类型选择结果寄存器
        lir_operand_t *result_operand;
        if (op->code == LIR_OPCODE_UREM || op->code == LIR_OPCODE_SREM) {
            // 余数在 AH
            result_operand = ah_operand; // AH寄存器
        } else {
            // 商在 AL
            result_operand = al_operand;
        }

        linked_push(list, lir_op_move(op->output, result_operand));

    } else {
        // 16位及以上的除法处理（原有逻辑）
        lir_operand_t *ax_operand = lir_reg_operand(rax->index, output_kind);
        lir_operand_t *dx_operand = lir_reg_operand(rdx->index, output_kind);

        // 清零高位寄存器
        if (output_kind == TYPE_UINT16 || output_kind == TYPE_INT16) {
            // 16位除法：清零DX
            linked_push(list, lir_op_new(LIR_OPCODE_CLR, NULL, NULL, lir_reg_operand(rdx->index, TYPE_UINT16)));
        } else {
            // 32位及以上：清零EDX/RDX
            linked_push(list, lir_op_new(LIR_OPCODE_CLR, NULL, NULL, dx_operand));
        }

        // 将被除数加载到AX/EAX/RAX
        linked_push(list, lir_op_move(ax_operand, op->first));

        // 选择结果寄存器
        lir_operand_t *result_operand = ax_operand; // 商
        if (op->code == LIR_OPCODE_UREM || op->code == LIR_OPCODE_SREM) {
            result_operand = dx_operand; // 余数
        }


        lir_operand_t *dividend_operand = lir_regs_operand(2, ax_operand->value, dx_operand->value);

        // 执行除法
        linked_push(list, lir_op_new(op_code, dividend_operand, op->second, result_operand));
        linked_push(list, lir_op_move(op->output, result_operand));
    }

    return list;
}

static linked_t *amd64_lower_safepoint(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    lir_operand_t *result_operand = operand_new(LIR_OPERAND_REG, r15);

    // 增加 label continue
    linked_push(list, lir_op_new(op->code, NULL, NULL, result_operand));

    return list;
}

/**
 * 对于 NEG float，添加 -0.0 作为 second 操作数
 * -0.0 的位表示就是符号位掩码 (0x80000000 for f32, 0x8000000000000000 for f64)
 * 后续由 lower_imm 转换为 var，用于 XOR 实现取反
 */
static void amd64_prepare_neg_float(lir_op_t *op) {
    if (op->code != LIR_OPCODE_NEG || !is_float(operand_type_kind(op->output))) {
        return;
    }

    type_kind kind = operand_type_kind(op->output);
    lir_imm_t *neg_zero_imm = NEW(lir_imm_t);
    neg_zero_imm->kind = kind;
    if (kind == TYPE_FLOAT64) {
        neg_zero_imm->f64_value = -0.0;
    } else {
        neg_zero_imm->f32_value = -0.0f;
    }
    op->second = operand_new(LIR_OPERAND_IMM, neg_zero_imm);
}

static void amd64_lower_block(closure_t *c, basic_block_t *block) {
    linked_t *operations = linked_new();
    LINKED_FOR(block->operations) {
        lir_op_t *op = LINKED_VALUE();

        amd64_prepare_neg_float(op);

        linked_t *symbol_operations = linked_new();
        linked_concat(operations, amd64_lower_imm(c, op, symbol_operations));
        if (symbol_operations->count > 0) {
            basic_block_t *first_block = c->blocks->take[0];
            linked_t *insert_operations = first_block->operations;
            if (block->id == first_block->id) {
                insert_operations = operations;
            }

            // maybe empty
            linked_node *insert_head = insert_operations->front->succ->succ; // safepoint

            for (linked_node *sym_node = symbol_operations->front;
                 sym_node != symbol_operations->rear; sym_node = sym_node->succ) {
                lir_op_t *sym_op = sym_node->value;
                insert_head = linked_insert_after(insert_operations, insert_head, sym_op);
            }
        }

        if (lir_op_call(op) && op->second->value != NULL) {
            linked_t *call_operations = amd64_lower_call(c, op);
            for (linked_node *call_node = call_operations->front; call_node != call_operations->rear;
                 call_node = call_node->succ) {
                lir_op_t *temp_op = call_node->value;

                if (temp_op->code == LIR_OPCODE_MOVE && !lir_can_mov(temp_op)) {
                    temp_op->first = amd64_convert_first_to_temp(c, operations, temp_op->first);
                    linked_push(operations, temp_op);
                    continue;
                }

                linked_push(operations, temp_op);
            }

            continue;
        }

        if (op->code == LIR_OPCODE_NEG) {
            linked_concat(operations, amd64_lower_neg(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_FN_BEGIN) {
            linked_t *fn_begin_operations = amd64_lower_fn_begin(c, op);
            for (linked_node *fn_begin_node = fn_begin_operations->front; fn_begin_node != fn_begin_operations->rear;
                 fn_begin_node = fn_begin_node->succ) {
                lir_op_t *temp_op = fn_begin_node->value;

                if (temp_op->code == LIR_OPCODE_MOVE && !lir_can_mov(temp_op)) {
                    temp_op->first = amd64_convert_first_to_temp(c, operations, temp_op->first);
                    linked_push(operations, temp_op);
                    continue;
                }

                linked_push(operations, temp_op);
            }

            continue;
        }

        if (op->code == LIR_OPCODE_RETURN) {
            linked_concat(operations, amd64_lower_return(c, op));
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

        if (lir_op_convert(op)) {
            if (op->code == LIR_OPCODE_UITOF) {
                lir_operand_t *ax_operand = lir_reg_operand(rax->index, operand_type_kind(op->first));
                linked_push(operations, lir_op_move(ax_operand, op->first));
                op->first = lir_reset_operand(ax_operand, LIR_FLAG_FIRST);
                linked_push(operations, op);
            } else if (op->first->assert_type != LIR_OPERAND_VAR) {
                op->first = amd64_convert_first_to_temp(c, operations, op->first);
                linked_push(operations, op);
            } else {
                linked_push(operations, op);
            }


            if (op->output->assert_type != LIR_OPERAND_VAR) {
                lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
                lir_operand_t *output = op->output;
                op->output = lir_reset_operand(temp, op->output->pos); // replace output
                linked_push(operations, lir_op_move(output, temp));
            }

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
