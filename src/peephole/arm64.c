#include "arm64.h"
#include "peephole.h"
#include "src/lir.h"
#include "src/types.h"
#include "utils/linked.h"
#include "utils/slice.h"


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
static bool peephole_move_elimination_match2(closure_t *c, basic_block_t *block, lir_op_t *op1, lir_op_t *op2) {
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

    // 检查临时变量是否只在 op2 中使用一次，且不在 live_out 中
    lir_var_t *temp_var = op1->output->value;
    if (!peephole_can_remove_var(block, temp_var)) {
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
static bool peephole_move_elimination_match1(closure_t *c, basic_block_t *block, lir_op_t *op1, lir_op_t *op2) {
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

    // 检查变量是否只在 op2 中使用一次，且不在 live_out 中
    lir_var_t *temp_var = op1->output->value;
    if (!peephole_can_remove_var(block, temp_var)) {
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
peephole_move_elimination(closure_t *c, basic_block_t *block, slice_t *ops, int *cursor, slice_t *new_ops) {
    // 如果没有下一条指令，直接保留当前指令
    if (*cursor + 1 >= ops->count) {
        return false;
    }

    lir_op_t *op1 = ops->take[*cursor];
    lir_op_t *op2 = ops->take[*cursor + 1];

    // match1: op1 被修改，op2 被移除
    if (peephole_move_elimination_match1(c, block, op1, op2)) {
        slice_push(new_ops, op1);
        *cursor += 2; // 跳过 op1 和 op2
        return true;
    }

    // match2: op1 被移除，op2 被修改
    if (peephole_move_elimination_match2(c, block, op1, op2)) {
        slice_push(new_ops, op2);
        *cursor += 2; // 跳过 op1 和 op2
        return true;
    }

    return false;
}

/**
 * 判断操作数类型是否为浮点数
 */
static bool is_float_operand(lir_operand_t *operand) {
    if (!operand) return false;
    type_t t = lir_operand_type(operand);
    return is_float(t.kind);
}

/**
 * 检查变量是否是浮点常量 2.0 的定义（通过 LIR_FLAG_CONST 标志）
 * 浮点数 lower 阶段会设置 var->flag 为 LIR_FLAG_CONST
 */
static bool is_float_var_const_two(lir_operand_t *operand) {
    if (!operand || operand->assert_type != LIR_OPERAND_VAR) {
        return false;
    }

    lir_var_t *var = operand->value;
    if (!(var->flag & FLAG(LIR_FLAG_CONST))) {
        return false;
    }

    // 检查浮点数 imm_value 是否为 2.0
    type_t t = lir_operand_type(operand);
    if (t.kind == TYPE_FLOAT64) {
        return var->imm_value.f64_value == 2.0;
    }
    if (t.kind == TYPE_FLOAT32) {
        return var->imm_value.f32_value == 2.0f;
    }

    return false;
}

/**
 * 检查 MOV 指令是否是 MOV IMM[2] -> VAR
 */
static bool is_mov_imm_two(lir_op_t *op, lir_operand_t **out_var) {
    if (op->code != LIR_OPCODE_MOVE) {
        return false;
    }
    if (!op->first || op->first->assert_type != LIR_OPERAND_IMM) {
        return false;
    }
    if (!op->output || op->output->assert_type != LIR_OPERAND_VAR) {
        return false;
    }

    lir_imm_t *imm = op->first->value;
    if (!is_integer(imm->kind)) {
        return false;
    }

    if (imm->int_value == 2) {
        *out_var = op->output;
        return true;
    }

    return false;
}

/**
 * ARM64 强度削减优化：将 x * 2 转换为 x + x
 * 
 * 浮点模式：
 *   MUL VAR[const_2.0], VAR[x] -> result  (VAR 带有 LIR_FLAG_CONST)
 *   -> ADD VAR[x], VAR[x] -> result
 * 
 * 整数模式：
 *   MOV IMM[2] -> VAR[t2]
 *   MUL VAR[t], VAR[t2] -> result
 *   -> ADD VAR[t], VAR[t] -> result  (如果 t2 只在 MUL 使用，则删除 MOV)
 */
static bool peephole_strength_reduction_mul2(closure_t *c, basic_block_t *block, slice_t *ops, int *cursor, slice_t *new_ops) {
    lir_op_t *op = ops->take[*cursor];

    // 检查是否是 MOV IMM[2] -> VAR 模式（整数）
    lir_operand_t *imm_two_var = NULL;
    if (is_mov_imm_two(op, &imm_two_var)) {
        if (*cursor + 1 >= ops->count) {
            return false;
        }

        lir_op_t *mul_op = ops->take[*cursor + 1];
        if (mul_op->code != LIR_OPCODE_MUL) {
            return false;
        }

        lir_operand_t *other_operand = NULL;
        if (lir_operand_equal(mul_op->first, imm_two_var)) {
            other_operand = mul_op->second;
        } else if (lir_operand_equal(mul_op->second, imm_two_var)) {
            other_operand = mul_op->first;
        }

        if (other_operand != NULL) {
            mul_op->code = LIR_OPCODE_ADD;
            mul_op->first = lir_reset_operand(other_operand, LIR_FLAG_FIRST);
            mul_op->second = lir_reset_operand(other_operand, LIR_FLAG_SECOND);

            lir_var_t *var = imm_two_var->value;
            if (!peephole_can_remove_var(block, var)) {
                slice_push(new_ops, op);
            }

            slice_push(new_ops, mul_op);
            *cursor += 2;
            return true;
        }
    }

    // 处理 MUL 指令（浮点模式）
    if (op->code != LIR_OPCODE_MUL) {
        return false;
    }

    if (is_float_var_const_two(op->first)) {
        // MUL const_2.0, x -> ADD x, x
        op->code = LIR_OPCODE_ADD;
        op->first = lir_reset_operand(op->second, LIR_FLAG_FIRST);
        slice_push(new_ops, op);
        (*cursor)++;
        return true;
    } else if (is_float_var_const_two(op->second)) {
        // MUL x, const_2.0 -> ADD x, x
        op->code = LIR_OPCODE_ADD;
        op->second = lir_reset_operand(op->first, LIR_FLAG_SECOND);
        slice_push(new_ops, op);
        (*cursor)++;
        return true;
    }

    return false;
}

/**
 * FMA 模式识别优化
 * 识别 MUL + ADD 或 MUL + SUB 模式并合并为 MADD/MSUB/FMADD/FMSUB/FNMSUB
 *
 * Pattern 1: MUL + ADD -> MADD/FMADD
 *   MUL first, second -> mul_result
 *   ADD mul_result, addend -> result  或  ADD addend, mul_result -> result
 *   => MADD/FMADD first, [second, addend] -> result
 *      result = addend + first * second
 *
 * Pattern 2a: MUL + SUB -> FNMSUB (仅浮点)
 *   MUL first, second -> mul_result
 *   SUB mul_result, subtrahend -> result  (mul_result 是被减数)
 *   => FNMSUB first, [second, subtrahend] -> result
 *      result = first * second - subtrahend
 *
 * Pattern 2b: MUL + SUB -> MSUB/FMSUB
 *   MUL first, second -> mul_result
 *   SUB minuend, mul_result -> result  (mul_result 是减数)
 *   => MSUB/FMSUB first, [second, minuend] -> result
 *      result = minuend - first * second
 */
static bool peephole_fma_recognition(closure_t *c, basic_block_t *block, slice_t *ops, int *cursor, slice_t *new_ops) {
    // 检查是否有足够的指令
    if (*cursor + 1 >= ops->count) {
        return false;
    }

    lir_op_t *mul_op = ops->take[*cursor];
    lir_op_t *next_op = ops->take[*cursor + 1];

    // 只处理 MUL 指令
    if (mul_op->code != LIR_OPCODE_MUL) {
        return false;
    }

    // MUL 必须有输出且是变量类型
    if (!mul_op->output || mul_op->output->assert_type != LIR_OPERAND_VAR) {
        return false;
    }

    // 检查下一条是否是 ADD 或 SUB
    if (next_op->code != LIR_OPCODE_ADD && next_op->code != LIR_OPCODE_SUB) {
        return false;
    }

    // 检查 MUL 输出是否只在下一条指令中使用一次
    // 需要检查整个 block，确保变量在 block 中只有一次使用，且不在 live_out 中
    lir_var_t *mul_out_var = mul_op->output->value;
    if (!peephole_can_remove_var(block, mul_out_var)) {
        return false;
    }

    // SUB ra, imm -> output 转换为 ADD ra, -imm -> output
    // 这样可以增加 FMA 命中率
    if (next_op->code == LIR_OPCODE_SUB && next_op->second &&
        next_op->second->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = next_op->second->value;
        // 创建取负后的立即数
        lir_imm_t *neg_imm = NEW(lir_imm_t);
        neg_imm->kind = imm->kind;
        if (imm->kind == TYPE_FLOAT32) {
            neg_imm->f32_value = -imm->f32_value;
        } else if (imm->kind == TYPE_FLOAT64) {
            neg_imm->f64_value = -imm->f64_value;
        } else {
            neg_imm->int_value = -imm->int_value;
        }
        // 将 SUB 转换为 ADD
        next_op->code = LIR_OPCODE_ADD;
        next_op->second->value = neg_imm;
    }

    // 确定 FMA 操作数
    lir_operand_t *mul_first = mul_op->first;
    lir_operand_t *mul_second = mul_op->second;
    lir_operand_t *mul_output = mul_op->output;
    lir_operand_t *other_operand = NULL; // addend 或 minuend
    bool is_valid_pattern = false;

    if (next_op->code == LIR_OPCODE_ADD) {
        // ADD: 检查哪个操作数是 MUL 的输出
        if (lir_operand_equal(next_op->first, mul_output)) {
            other_operand = next_op->second; // ADD(mul_result, addend)
            is_valid_pattern = true;
        } else if (lir_operand_equal(next_op->second, mul_output)) {
            other_operand = next_op->first; // ADD(addend, mul_result)
            is_valid_pattern = true;
        }
    } else if (next_op->code == LIR_OPCODE_SUB) {
        // SUB Pattern 2a: SUB(mul_result, subtrahend) -> FNMSUB
        // result = mul_result - subtrahend = (first * second) - subtrahend
        // FNMSUB 计算 Rd = Rn * Rm - Ra
        if (lir_operand_equal(next_op->first, mul_output)) {
            other_operand = next_op->second; // subtrahend
            is_valid_pattern = true;
        }
        // SUB Pattern 2b: SUB(minuend, mul_result) -> FMSUB
        // result = minuend - mul_result = minuend - (first * second)
        // FMSUB 计算 Rd = Ra - Rn * Rm
        else if (lir_operand_equal(next_op->second, mul_output)) {
            other_operand = next_op->first; // minuend
            is_valid_pattern = true;
        }
    }

    if (!is_valid_pattern || !other_operand) {
        return false;
    }

    // 确定是浮点还是整数 FMA
    bool is_float = is_float_operand(mul_first);
    lir_opcode_t fma_opcode;

    if (next_op->code == LIR_OPCODE_ADD) {
        fma_opcode = is_float ? ARM64_OPCODE_FMADD : ARM64_OPCODE_MADD;
    } else {
        // SUB 模式：区分 FNMSUB 和 FMSUB
        if (lir_operand_equal(next_op->first, mul_output)) {
            // Pattern 2a: SUB(mul_result, x) -> (a*b) - x -> FNMSUB
            // FNMSUB 只有浮点版本，整数没有对应指令
            if (!is_float) {
                return false;
            }
            fma_opcode = ARM64_OPCODE_FNMSUB;
        } else {
            // Pattern 2b: SUB(x, mul_result) -> x - (a*b) -> FMSUB/MSUB
            fma_opcode = is_float ? ARM64_OPCODE_FMSUB : ARM64_OPCODE_MSUB;
        }
    }

    // 经过 lower 后，mul 的 first 和 second 必须是 VAR 类型
    assert(mul_first->assert_type == LIR_OPERAND_VAR);
    assert(mul_second->assert_type == LIR_OPERAND_VAR);

    // 经过 lower 后，只有 ADD 的 second 可能是 IMM
    // 整数模式下，如果 other_operand 是 IMM，额外的 MOV 会抵消 FMA 收益，跳过
    // 浮点模式下，FMOV imm8 可能更高效，仍然进行转换
    if (other_operand->assert_type != LIR_OPERAND_VAR) {
        if (!is_float) {
            // 整数模式下不进行 FMA 转换
            return false;
        }
        // 浮点模式下，生成 MOV 指令将立即数加载到临时变量
        lir_operand_t *temp = temp_var_operand(c->module, lir_operand_type(other_operand));
        slice_push(new_ops, lir_op_move(temp, other_operand));
        other_operand = lir_reset_operand(temp, LIR_FLAG_ADDEND);
    }

    // 修改 next_op 为 FMA 指令
    // first: mul_first (Rn), second: mul_second (Rm), addend: other_operand (Ra)
    next_op->code = fma_opcode;
    next_op->first = lir_reset_operand(mul_first, LIR_FLAG_FIRST);
    next_op->second = lir_reset_operand(mul_second, LIR_FLAG_SECOND);
    next_op->addend = lir_reset_operand(other_operand, LIR_FLAG_ADDEND);
    set_operand_flag(next_op->addend);
    // output 保持不变

    // 不保留 MUL 指令，只保留转换后的 FMA 指令
    slice_push(new_ops, next_op);
    *cursor += 2; // 跳过 MUL 和 ADD/SUB 指令

    return true;
}

static inline void arm64_peephole_handle_block(closure_t *c, basic_block_t *block) {
    peephole_build_use_count(block);

    slice_t *ops = slice_new();

    LINKED_FOR(block->operations) {
        lir_op_t *op = LINKED_VALUE();
        slice_push(ops, op);
    }

    int iterations = 0;
    int max_iterations = 5; // 防止无限循环

    bool changed = true;
    while (changed && iterations < max_iterations) {
        changed = false;
        iterations++;

        slice_t *new_ops = slice_new();
        int cursor = 0;
        while (cursor < ops->count) {
            if (peephole_fma_recognition(c, block, ops, &cursor, new_ops)) {
                changed = true;
                continue;
            }

            if (peephole_strength_reduction_mul2(c, block, ops, &cursor, new_ops)) {
                changed = true;
                continue;
            }

            if (peephole_move_elimination(c, block, ops, &cursor, new_ops)) {
                changed = true;
                continue;
            }

            assert(ops->take[cursor]);
            slice_push(new_ops, ops->take[cursor]);
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


void arm64_peephole_pre(closure_t *c) {
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        arm64_peephole_handle_block(c, block);
    }
}
