#include "amd64.h"
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
 * AMD64 强度削减优化：将 x * 2 转换为 x + x
 * 
 * 匹配模式：
 * mul first, 2 -> output
 * 
 * 优化为：
 * add first, first -> output
 */
static bool peephole_strength_reduction_mul2(basic_block_t *block) {
    bool optimized = false;
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
            op->second = lir_reset_operand(op->first, LIR_FLAG_SECOND);
            optimized = true;
        } else if (is_imm_two(op->first)) {
            // mul 2, second -> add second, second
            op->code = LIR_OPCODE_ADD;
            op->first = lir_reset_operand(op->second, LIR_FLAG_FIRST);
            optimized = true;
        }

        current = current->succ;
    }

    return optimized;
}

void amd64_peephole_pre(closure_t *c) {
    bool changed = true;
    int iterations = 0;
    const int max_iterations = 10;

    while (changed && iterations < max_iterations) {
        changed = false;
        iterations++;

        // 遍历所有基本块并应用窥孔优化
        for (int i = 0; i < c->blocks->count; ++i) {
            basic_block_t *block = c->blocks->take[i];

            // 应用强度削减优化 (x * 2 -> x + x)
            if (peephole_strength_reduction_mul2(block)) {
                changed = true;
            }
        }
    }
}
