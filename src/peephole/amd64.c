#include "amd64.h"
#include "peephole.h"
#include "src/lir.h"
#include "src/types.h"
#include "utils/linked.h"
#include "utils/slice.h"

static bool is_valid_lea_scale(int64_t scale) {
    return scale == 1 || scale == 2 || scale == 4 || scale == 8;
}

/**
 * LEA 融合优化：识别 MUL + [MOVE] + ADD/SUB 模式并融合为 LEA 指令
 *
 * 模式 1: MUL + ADD/SUB (2 条指令)
 *   MUL VAR[a], IMM[scale] -> VAR[t1]     ; scale ∈ {1, 2, 4, 8}
 *   ADD VAR[t1], IMM[disp] -> VAR[dst]    ; or SUB (disp 取负)
 *   => LEA indirect_addr(index=a, scale=scale, offset=±disp) -> VAR[dst]
 *
 * 模式 2: MUL + MOVE + ADD/SUB (3 条指令)
 *   MUL VAR[a], IMM[scale] -> VAR[t1]
 *   MOVE VAR[t1] -> VAR[t2]
 *   ADD VAR[t2], IMM[disp] -> VAR[dst]
 *   => LEA indirect_addr(index=a, scale=scale, offset=±disp) -> VAR[dst]
 */
static bool peephole_lea_fusion(closure_t *c, basic_block_t *block, slice_t *ops, int *cursor, slice_t *new_ops) {
    lir_op_t *mul_op = ops->take[*cursor];

    // 第一条必须是 MUL 指令
    if (mul_op->code != LIR_OPCODE_MUL) {
        return false;
    }

    // MUL 的第二个操作数必须是立即数 (scale)
    if (!mul_op->second || mul_op->second->assert_type != LIR_OPERAND_IMM) {
        return false;
    }

    // MUL 的第一个操作数必须是变量
    if (!mul_op->first || mul_op->first->assert_type != LIR_OPERAND_VAR) {
        return false;
    }

    // MUL 的输出必须是变量
    if (!mul_op->output || mul_op->output->assert_type != LIR_OPERAND_VAR) {
        return false;
    }

    // 检查 scale 是否有效 (1, 2, 4, 8)
    lir_imm_t *scale_imm = mul_op->second->value;
    if (!is_integer(scale_imm->kind)) {
        return false;
    }
    int64_t scale = scale_imm->int_value;
    if (!is_valid_lea_scale(scale)) {
        return false;
    }

    // 只处理整数类型
    type_t mul_type = lir_operand_type(mul_op->output);
    if (!is_integer(mul_type.map_imm_kind)) {
        return false;
    }

    // 查找后续的 ADD/SUB 指令 (可能有中间的 MOVE)
    lir_op_t *add_sub_op = NULL;
    lir_operand_t *mul_result_var = mul_op->output;
    int consumed_count = 1; // 已消耗的指令数

    // 检查下一条指令
    if (*cursor + 1 >= ops->count) {
        return false;
    }

    lir_op_t *next_op = ops->take[*cursor + 1];

    // 模式 1: MUL 直接跟 ADD/SUB
    if (next_op->code == LIR_OPCODE_ADD || next_op->code == LIR_OPCODE_SUB) {
        // 检查 ADD/SUB 的第一个操作数是否是 MUL 的输出
        if (next_op->first && lir_operand_equal(next_op->first, mul_result_var)) {
            add_sub_op = next_op;
            consumed_count = 2;
        }
    }
    // 模式 2: MUL + MOVE + ADD/SUB
    else if (next_op->code == LIR_OPCODE_MOVE) {
        // 检查 MOVE 的输入是否是 MUL 的输出
        if (next_op->first && lir_operand_equal(next_op->first, mul_result_var)) {
            // MUL 的输出变量必须可以被删除
            lir_var_t *mul_out_var = mul_result_var->value;
            if (!peephole_can_remove_var(block, mul_out_var)) {
                return false;
            }

            // 检查第三条指令
            if (*cursor + 2 >= ops->count) {
                return false;
            }

            lir_op_t *third_op = ops->take[*cursor + 2];
            if (third_op->code == LIR_OPCODE_ADD || third_op->code == LIR_OPCODE_SUB) {
                // 检查 ADD/SUB 的第一个操作数是否是 MOVE 的输出
                if (third_op->first && lir_operand_equal(third_op->first, next_op->output)) {
                    add_sub_op = third_op;
                    mul_result_var = next_op->output; // 更新为 MOVE 的输出
                    consumed_count = 3;
                }
            }
        }
    }

    if (!add_sub_op) {
        return false;
    }

    // ADD/SUB 的第二个操作数必须是立即数 (displacement)
    if (!add_sub_op->second || add_sub_op->second->assert_type != LIR_OPERAND_IMM) {
        return false;
    }

    // ADD/SUB 的输出必须是变量
    if (!add_sub_op->output || add_sub_op->output->assert_type != LIR_OPERAND_VAR) {
        return false;
    }

    // 中间变量必须可以被删除
    // 但如果中间变量和最终输出是同一个变量（如 t = t - 1），则不需要检查
    // 因为 LEA 会接管这个变量的定义
    lir_var_t *temp_var = mul_result_var->value;
    if (!lir_operand_equal(mul_result_var, add_sub_op->output)) {
        if (!peephole_can_remove_var(block, temp_var)) {
            return false;
        }
    }

    // 获取 displacement
    lir_imm_t *disp_imm = add_sub_op->second->value;
    if (!is_integer(disp_imm->kind)) {
        return false;
    }
    int64_t disp = disp_imm->int_value;
    if (add_sub_op->code == LIR_OPCODE_SUB) {
        disp = -disp;
    }

    // 检查 disp 是否在 32 位有符号范围内
    if (disp < INT32_MIN || disp > INT32_MAX) {
        return false;
    }

    // 创建 LEA 指令
    // LEA 的 first 是 indirect_addr，包含 index、scale 和 offset
    lir_indirect_addr_t *addr = NEW(lir_indirect_addr_t);
    addr->index = lir_reset_operand(mul_op->first, LIR_FLAG_FIRST); // index = a
    addr->offset = (int64_t) disp;
    addr->type = mul_type;

    // scale=2 时，使用 lea (reg, reg, 1) 代替 lea (, reg, 2)
    // 这样 base=index, scale=1, 地址计算为 reg + reg*1 = reg*2
    if (scale == 2) {
        addr->base = lir_reset_operand(mul_op->first, LIR_FLAG_FIRST); // base = a
        addr->scale = 1;
    } else {
        addr->base = NULL; // 不使用 base
        addr->scale = (int) scale;
    }

    lir_operand_t *addr_operand = operand_new(LIR_OPERAND_INDIRECT_ADDR, addr);
    addr_operand->pos = LIR_FLAG_FIRST;

    lir_op_t *lea_op = lir_op_new(LIR_OPCODE_LEA, addr_operand, NULL, add_sub_op->output);
    lea_op->line = mul_op->line;
    lea_op->column = mul_op->column;

    slice_push(new_ops, lea_op);
    *cursor += consumed_count;

    return true;
}

static inline void amd64_peephole_handle_block(closure_t *c, basic_block_t *block) {
    peephole_build_use_count(block);

    slice_t *ops = slice_new();

    LINKED_FOR(block->operations) {
        lir_op_t *op = LINKED_VALUE();
        slice_push(ops, op);
    }

    int iterations = 0;
    int max_iterations = 5;

    bool changed = true;
    while (changed && iterations < max_iterations) {
        changed = false;
        iterations++;

        slice_t *new_ops = slice_new();
        int cursor = 0;
        while (cursor < ops->count) {
            if (peephole_lea_fusion(c, block, ops, &cursor, new_ops)) {
                changed = true;
                continue;
            }

            lir_op_t *op = ops->take[cursor];
            slice_push(new_ops, op);
            cursor++;
        }

        slice_free(ops);
        ops = new_ops;
    }

    linked_t *new_operations = linked_new();
    for (int i = 0; i < ops->count; i++) {
        lir_op_t *op = ops->take[i];
        assert(op);
        linked_push(new_operations, op);
    }

    slice_free(ops);
    linked_free(block->operations);
    block->operations = new_operations;
    lir_set_quick_op(block);
}

void amd64_peephole_pre(closure_t *c) {
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        amd64_peephole_handle_block(c, block);
    }
}
