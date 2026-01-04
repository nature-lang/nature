#include "peephole.h"


static bool is_imm_two(lir_operand_t *operand) {
    if (!operand || operand->assert_type != LIR_OPERAND_IMM) {
        return false;
    }

    lir_imm_t *imm = operand->value;
    if (is_integer(imm->kind)) {
        return imm->int_value == 2 || imm->uint_value == 2;
    }
    if (imm->kind == TYPE_FLOAT64) {
        return imm->f64_value == 2.0;
    }
    if (imm->kind == TYPE_FLOAT32) {
        return imm->f32_value == 2.0f;
    }
    return false;
}

/**
 * 强度削减优化：将 x * 2 转换为 x + x
 * 
 * 匹配模式：
 * mul first, 2 -> output
 * 
 * 优化为：
 * add first, first -> output
 * 
 * 适用于整数和浮点数类型
 */
static bool peephole_strength_reduction_mul2(closure_t *c) {
    bool optimized = false;

    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        linked_t *operations = block->operations;

        linked_node *current = linked_first(operations);
        while (current != NULL && current->value != NULL) {
            lir_op_t *op = current->value;

            // 只处理 MUL 指令
            if (op->code != LIR_OPCODE_MUL) {
                current = current->succ;
                continue;
            }

            if (is_imm_two(op->second)) {
                // mul first, 2 -> add first, first
                op->code = LIR_OPCODE_ADD;
                op->second = lir_reset_operand(op->first, LIR_FLAG_SECOND); // 复用第一个操作数
                optimized = true;
            } else if (is_imm_two(op->first)) {
                // mul 2, second -> add second, second
                op->code = LIR_OPCODE_ADD;
                op->first = lir_reset_operand(op->second, LIR_FLAG_FIRST);
                optimized = true;
            }

            current = current->succ;
        }
    }

    return optimized;
}

/**
 * mov x0 -> temp_var
 * XXX use_reg, temp_var -> x0
 * ---
 * XXX use_reg, x0 -> x0
 *
 * or
 *
 * mov x0 -> temp_var
 * XXX temp_var, use_reg -> x0
 * ---
 * XXX x0, use_reg -> x0
 */
static bool peephole_move_elimination_match2(closure_t *c, lir_op_t *op1, lir_op_t *op2, table_t *use) {
    // 第一个指令必须是MOVE指令
    if (op1->code != LIR_OPCODE_MOVE) {
        return false;
    }

    if (!lir_can_mov_eliminable(op2->code)) {
        return false;
    }

    // MOVE指令必须有输入和输出操作数
    if (op1->first == NULL || op1->output == NULL) {
        return false;
    }

    // MOVE指令的输出必须是变量类型
    if (op1->output->assert_type != LIR_OPERAND_VAR) {
        return false;
    }

    // 检查临时变量是否在后续被使用，如果后续被使用则不允许删除。
    lir_var_t *temp_var = op1->output->value;
    bool use_later = table_exist(use, temp_var->ident);
    if (use_later) {
        return false;
    }

    // 情况1: XXX use_reg, temp_var -> x0
    // 优化为: XXX use_reg, x0 -> x0  (其中x0是MOVE的输入)
    if (op2->second != NULL && lir_operand_equal(op2->second, op1->output)) {
        // 第二个指令的输出必须等于MOVE指令的输入
        if (!lir_operand_equal(op2->output, op1->first)) {
            return false;
        }

        // 执行优化：将第二个指令的second操作数替换为MOVE的输入
        op2->second = lir_reset_operand(op1->first, LIR_FLAG_SECOND);
        return true;
    } else if (op2->first != NULL && lir_operand_equal(op2->first, op1->output)) {
        // 第二个指令的输出必须等于MOVE指令的输入
        if (!lir_operand_equal(op2->output, op1->first)) {
            return false;
        }

        // 执行优化：将第二个指令的first操作数替换为MOVE的输入
        op2->first = lir_reset_operand(op1->first, LIR_FLAG_FIRST);
        return true;
    }

    return false;
}


/**
 * XXX use_reg, use_var -> def_temp_var
 * mov def_temp_var -> x0
 * ---
 * XXX use_reg, use_var -> x0 (use mov id and resolve  char)
 *
 */
static bool peephole_move_elimination_match1(closure_t *c, lir_op_t *op1, lir_op_t *op2, table_t *use) {
    if (!lir_can_mov_eliminable(op1->code)) {
        return false;
    }

    if (op2->code != LIR_OPCODE_MOVE) {
        return false;
    }

    if (op1->output == NULL) {
        return false;
    }

    if (op2->first == NULL) {
        return false;
    }

    if (op2->output->assert_type != LIR_OPERAND_VAR && op2->output->assert_type != LIR_OPERAND_REG) {
        return false;
    }

    if (op2->first->assert_type != LIR_OPERAND_VAR || op1->output->assert_type != LIR_OPERAND_VAR) {
        return false;
    }

    lir_var_t *output_var = op1->output->value;
    if (output_var->flag & FLAG(LIR_FLAG_CONST)) {
        return false;
    }

    if (!lir_operand_equal(op1->output, op2->first)) {
        return false;
    }

    // check in use, if in, cannot elimination
    lir_var_t *temp_var = op1->output->value;
    bool use_later = table_exist(use, temp_var->ident);
    if (use_later) {
        return false;
    }


    op1->output = lir_reset_operand(op2->output, LIR_FLAG_OUTPUT);
    op1->resolve_char = op2->resolve_char;
    return true;
}

/**
 * 窥孔优化：识别 XXX VAR -> MOVE VAR, REG|VAR 模式并优化为 XXX -> REG|VAR
 * 
 * 优化模式：
 * 指令1: XXX first, second -> var_temp|reg_temp|indirect_addr_temp
 * 指令2: MOVE var_temp|reg_temp|indirect_addr_temp -> reg|var
 * 
 * 优化为：
 * 指令1: XXX first, second -> reg|var
 */
static bool
peephole_move_elimination(closure_t *c) {
    bool optimized = false;

    table_t *use = table_new();

    for (int i = c->blocks->count - 1; i >= 0; --i) {
        basic_block_t *block = c->blocks->take[i];
        linked_t *operations = block->operations;

        linked_node *current = linked_last(block->operations);
        while (current != NULL) {
            lir_op_t *op2 = current->value;
            if (op2->code == LIR_OPCODE_LABEL) {
                break;
            }

            lir_op_t *op1 = current->prev->value;

            if (peephole_move_elimination_match1(c, op1, op2, use)) {
                linked_node *prev = current->prev;
                linked_remove(operations, current);
                current = prev;
                optimized = true;

                continue;
            }

            if (peephole_move_elimination_match2(c, op1, op2, use)) {
                linked_remove(operations, current->prev);
                optimized = true;
                continue;
            }

            // check
            slice_t *use_operands = extract_op_operands(op2, FLAG(LIR_OPERAND_VAR), FLAG(LIR_FLAG_USE), true);
            for (int j = 0; j < use_operands->count; ++j) {
                lir_var_t *var = use_operands->take[j];
                table_set(use, var->ident, var);
            }

            current = current->prev;
        }
    }


    return optimized;
}

void peephole_pre(closure_t *c) {
    bool changed = true;
    int iterations = 0;
    const int max_iterations = 10; // 防止无限循环

    // 迭代优化直到没有更多改变或达到最大迭代次数
    while (changed && iterations < max_iterations) {
        changed = false;
        iterations++;

        // 应用强度削减优化 (x * 2 -> x + x)
        if (peephole_strength_reduction_mul2(c)) {
            changed = true;
        }
    }
}


void peephole_post(closure_t *c) {
    bool changed = true;
    int iterations = 0;
    const int max_iterations = 10; // 防止无限循环

    // 迭代优化直到没有更多改变或达到最大迭代次数
    while (changed && iterations < max_iterations) {
        changed = false;
        iterations++;

        // 应用移动消除优化, amd64 不适用
        if (BUILD_ARCH != ARCH_AMD64) {
            if (peephole_move_elimination(c)) {
                changed = true;
            }
        }
    }
}
