#include "peephole.h"

/**
 * 检查操作数是否相等
 */
static bool operands_equal(lir_operand_t *op1, lir_operand_t *op2) {
    if (!op1 || !op2) {
        return false;
    }

    if (op1->assert_type != op2->assert_type) {
        return false;
    }

    if (op1->assert_type == LIR_OPERAND_VAR) {
        lir_var_t *var1 = op1->value;
        lir_var_t *var2 = op2->value;
        return strcmp(var1->ident, var2->ident) == 0;
    }

    if (op1->assert_type == LIR_OPERAND_REG) {
        reg_t *reg1 = op1->value;
        reg_t *reg2 = op2->value;
        return reg1->index == reg2->index;
    }

    if (op1->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *addr1 = op1->value;
        lir_indirect_addr_t *addr2 = op2->value;
        return operands_equal(addr1->base, addr2->base) && addr1->offset == addr2->offset;
    }

    return false;
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

    if (op2->code != LIR_OPCODE_MOVE &&
        op2->code != LIR_OPCODE_SUB &&
        op2->code != LIR_OPCODE_ADD &&
        op2->code != LIR_OPCODE_MUL &&
        op2->code != LIR_OPCODE_UDIV &&
        op2->code != LIR_OPCODE_SDIV &&
        op2->code != LIR_OPCODE_UREM &&
        op2->code != LIR_OPCODE_SREM &&
        op2->code != LIR_OPCODE_NEG &&
        op2->code != LIR_OPCODE_SSHR &&
        op2->code != LIR_OPCODE_USHR &&
        op2->code != LIR_OPCODE_USHL &&
        op2->code != LIR_OPCODE_AND &&
        op2->code != LIR_OPCODE_OR &&
        op2->code != LIR_OPCODE_XOR &&
        op2->code != LIR_OPCODE_NOT &&
        op2->code != LIR_OPCODE_USLT &&
        op2->code != LIR_OPCODE_SLT &&
        op2->code != LIR_OPCODE_SLE &&
        op2->code != LIR_OPCODE_SGT &&
        op2->code != LIR_OPCODE_SGE &&
        op2->code != LIR_OPCODE_SEE &&
        op2->code != LIR_OPCODE_SNE) {
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

    // 检查临时变量是否在后续被使用
    lir_var_t *temp_var = op1->output->value;
    bool use_later = table_exist(use, temp_var->ident);
    if (use_later) {
        return false;
    }

    // 情况1: XXX use_reg, temp_var -> x0
    // 优化为: XXX use_reg, x0 -> x0  (其中x0是MOVE的输入)
    if (op2->second != NULL && operands_equal(op2->second, op1->output)) {
        // 第二个指令的输出必须等于MOVE指令的输入
        if (!operands_equal(op2->output, op1->first)) {
            return false;
        }

        // 执行优化：将第二个指令的second操作数替换为MOVE的输入
        op2->second = op1->first;
        return true;
    } else if (op2->first != NULL && operands_equal(op2->first, op1->output)) {
        // 第二个指令的输出必须等于MOVE指令的输入
        if (!operands_equal(op2->output, op1->first)) {
            return false;
        }

        // 执行优化：将第二个指令的first操作数替换为MOVE的输入
        op2->first = op1->first;
        return true;
    }

    return false;
}


/**
 * XXX use_reg, use_var -> def_temp_var
 * mov def_temp_var -> x0
 * ---
 * XXX use_reg, use_var -> x0
 *
 */
static bool peephole_move_elimination_match1(closure_t *c, lir_op_t *op1, lir_op_t *op2, table_t *use) {
    if (op1->code != LIR_OPCODE_MOVE &&
        op1->code != LIR_OPCODE_SUB &&
        op1->code != LIR_OPCODE_ADD &&
        op1->code != LIR_OPCODE_MUL &&
        op1->code != LIR_OPCODE_UDIV &&
        op1->code != LIR_OPCODE_SDIV &&
        op1->code != LIR_OPCODE_UREM &&
        op1->code != LIR_OPCODE_SREM &&
        op1->code != LIR_OPCODE_NEG &&
        op1->code != LIR_OPCODE_SSHR &&
        op1->code != LIR_OPCODE_USHR &&
        op1->code != LIR_OPCODE_USHL &&
        op1->code != LIR_OPCODE_AND &&
        op1->code != LIR_OPCODE_OR &&
        op1->code != LIR_OPCODE_XOR &&
        op1->code != LIR_OPCODE_NOT &&
        op1->code != LIR_OPCODE_USLT &&
        op1->code != LIR_OPCODE_SLT &&
        op1->code != LIR_OPCODE_SLE &&
        op1->code != LIR_OPCODE_SGT &&
        op1->code != LIR_OPCODE_SGE &&
        op1->code != LIR_OPCODE_SEE &&
        op1->code != LIR_OPCODE_SNE) {
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

    if (!operands_equal(op1->output, op2->first)) {
        return false;
    }

    // check in use, if in, cannot elimination
    lir_var_t *temp_var = op1->output->value;
    bool use_later = table_exist(use, temp_var->ident);
    if (use_later) {
        return false;
    }

    op1->output = op2->output;
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

void peephole_optimize(closure_t *c) {
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
