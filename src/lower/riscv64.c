#include "riscv64.h"
#include "lower.h"
#include "riscv64_abi.h"
#include "src/register/arch/riscv64.h"

static lir_operand_t *riscv64_convert_use_var(closure_t *c, linked_t *list, lir_operand_t *operand) {
    assert(c);
    assert(list);
    assert(operand);

    lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(operand));
    assert(temp);

    linked_push(list, lir_op_move(temp, operand));
    return lir_reset_operand(temp, operand->pos);
}

/**
 * 将 symbol 转换为 la 指令形式 (RISC-V 中的加载地址指令)
 */
static lir_operand_t *riscv64_convert_lea_symbol_var(closure_t *c, linked_t *list, lir_operand_t *symbol_var_operand) {
    assert(c);
    assert(list);
    assert(symbol_var_operand);
    assert(c->module);

    lir_operand_t *symbol_ptr = temp_var_operand_with_alloc(c->module, type_kind_new(TYPE_ANYPTR));
    assert(symbol_ptr);

    linked_push(list, lir_op_lea(symbol_ptr, symbol_var_operand));

    // result 集成原始的 type
    lir_operand_t *result = indirect_addr_operand(c->module, lir_operand_type(symbol_var_operand), symbol_ptr, 0);
    assert(result);

    return lir_reset_operand(result, symbol_var_operand->pos);
}

static linked_t *riscv64_lower_imm(closure_t *c, lir_op_t *op, linked_t *symbol_operations) {
    assert(c);
    assert(op);

    linked_t *list = linked_new();
    assert(list);

    slice_t *imm_operands = extract_op_operands(op, FLAG(LIR_OPERAND_IMM), 0, false);
    assert(imm_operands);
    assert(imm_operands->take);

    for (int i = 0; i < imm_operands->count; ++i) {
        lir_operand_t *imm_operand = imm_operands->take[i];
        assert(imm_operand);
        lir_imm_t *imm = imm_operand->value;
        assert(imm);
        if (imm->kind != TYPE_RAW_STRING && !is_float(imm->kind)) {
            continue;
        }

        lower_imm_symbol(c, imm_operand, list, symbol_operations);
    }

    return list;
}

/**
 * 在 RISC-V 中处理符号变量
 * RISC-V 使用 la 指令加载符号地址，这需要处理为两条指令：auipc + addi
 */
static linked_t *riscv64_lower_symbol_var(closure_t *c, lir_op_t *op) {
    assert(c);
    assert(op);

    linked_t *list = linked_new();
    assert(list);

    if (op->code == LIR_OPCODE_LEA ||
        op->code == LIR_OPCODE_LABEL ||
        op->code == LIR_OPCODE_BAL) {
        return list;
    }

    // beq 需要特殊处理
    if (lir_op_branch_cmp(op)) {
        if (op->first && op->first->assert_type == LIR_OPERAND_SYMBOL_VAR) {
            op->first = riscv64_convert_lea_symbol_var(c, list, op->first);
        }

        if (op->second && op->second->assert_type == LIR_OPERAND_SYMBOL_VAR) {
            op->second = riscv64_convert_lea_symbol_var(c, list, op->second);
        }

        return list;
    }


    if (op->first && op->first->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        op->first = riscv64_convert_lea_symbol_var(c, list, op->first);
    }

    if (op->second && op->second->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        op->second = riscv64_convert_lea_symbol_var(c, list, op->second);
    }

    if (op->output && op->output->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        op->output = riscv64_convert_lea_symbol_var(c, list, op->output);
    }

    return list;
}

/**
 * 根据RISC-V规范处理比较指令
 * RISC-V中的比较通常通过分支指令或者set指令实现
 */
static linked_t *riscv64_lower_cmp(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    // RISC-V中比较操作的两个操作数都需要在寄存器中
    if (op->first->assert_type != LIR_OPERAND_VAR) {
        op->first = riscv64_convert_use_var(c, list, op->first);
    }

    if (op->second->assert_type != LIR_OPERAND_VAR && op->second->assert_type != LIR_OPERAND_IMM) {
        op->second = riscv64_convert_use_var(c, list, op->second);
    }

    linked_push(list, op);

    // 处理需要设置条件码的情况
    if (lir_op_scc(op) && op->output->assert_type != LIR_OPERAND_VAR) {
        lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
        assert(temp);

        lir_operand_t *dst = op->output;
        op->output = lir_reset_operand(temp, op->output->pos);

        linked_push(list, lir_op_move(dst, op->output));
    }

    return list;
}

/**
 * 处理三元运算（算术和逻辑操作）
 */
static linked_t *riscv64_lower_ternary(closure_t *c, lir_op_t *op) {
    assert(op->first && op->output);

    linked_t *list = linked_new();

    // 确保操作数在寄存器中
    if (op->first->assert_type != LIR_OPERAND_VAR) {
        op->first = riscv64_convert_use_var(c, list, op->first);
    }

    // RISC-V对这些指令要求两个操作数都在寄存器中
    if (op->code == LIR_OPCODE_MUL ||
        op->code == LIR_OPCODE_UDIV ||
        op->code == LIR_OPCODE_UREM ||
        op->code == LIR_OPCODE_SDIV ||
        op->code == LIR_OPCODE_SREM ||
        op->code == LIR_OPCODE_XOR ||
        op->code == LIR_OPCODE_OR ||
        op->code == LIR_OPCODE_AND) {
        op->second = riscv64_convert_use_var(c, list, op->second);
    }

    linked_push(list, op);

    // 确保输出操作数是变量（可分配到寄存器）
    if (op->output->assert_type != LIR_OPERAND_VAR) {
        lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
        assert(temp);

        lir_operand_t *dst = op->output;
        op->output = lir_reset_operand(temp, op->output->pos);

        linked_push(list, lir_op_move(dst, op->output));
    }

    return list;
}

/**
 * 处理安全点指令
 */
static linked_t *riscv64_lower_safepoint(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    linked_push(list, op);

    return list;
}

/**
 * 处理加载有效地址指令
 * RISC-V使用la伪指令（auipc+addi）或者lui+addi实现
 */
static linked_t *riscv64_lower_lea(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    linked_push(list, op);

    // 确保输出是变量（可分配到寄存器）
    if (op->output->assert_type != LIR_OPERAND_VAR) {
        lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
        assert(temp);

        lir_operand_t *dst = op->output;
        op->output = lir_reset_operand(temp, op->output->pos);

        linked_push(list, lir_op_move(dst, op->output));
    }

    return list;
}

/**
 * 处理基本块中的所有指令
 */
static void riscv64_lower_block(closure_t *c, basic_block_t *block) {
    assert(c);
    assert(block);

    linked_t *operations = linked_new();
    LINKED_FOR(block->operations) {
        lir_op_t *op = LINKED_VALUE();

        linked_t *symbol_operations = linked_new();
        linked_concat(operations, riscv64_lower_imm(c, op, symbol_operations));
        if (symbol_operations->count > 0) {
            basic_block_t *first_block = c->blocks->take[0];
            linked_t *insert_operations = first_block->operations;
            if (block->id == first_block->id) {
                insert_operations = operations;
            }

            // maybe empty
            linked_node *insert_head = insert_operations->front->succ->succ; // safepoint

            for (linked_node *sym_node = symbol_operations->front; sym_node != symbol_operations->rear; sym_node = sym_node->succ) {
                lir_op_t *sym_op = sym_node->value;
                insert_head = linked_insert_after(insert_operations, insert_head, sym_op);
            }
        }

        linked_concat(operations, riscv64_lower_symbol_var(c, op));

        if (lir_op_call(op) && op->second->value != NULL) {
            linked_t *call_operations = riscv64_lower_call(c, op);
            for (linked_node *call_node = call_operations->front; call_node != call_operations->rear;
                 call_node = call_node->succ) {
                lir_op_t *temp_op = call_node->value;
                linked_concat(operations, riscv64_lower_symbol_var(c, temp_op));

                if (temp_op->code == LIR_OPCODE_MOVE && !lir_can_mov(temp_op)) {
                    temp_op->first = riscv64_convert_use_var(c, operations, temp_op->first);
                    linked_push(operations, temp_op);
                    continue;
                }

                linked_push(operations, temp_op);
            }
            continue;
        }

        if (op->code == LIR_OPCODE_FN_BEGIN) {
            linked_t *fn_begin_operations = riscv64_lower_fn_begin(c, op);
            for (linked_node *fn_begin_node = fn_begin_operations->front; fn_begin_node != fn_begin_operations->rear;
                 fn_begin_node = fn_begin_node->succ) {
                lir_op_t *temp_op = fn_begin_node->value;
                linked_concat(operations, riscv64_lower_symbol_var(c, temp_op));

                if (temp_op->code == LIR_OPCODE_MOVE && !lir_can_mov(temp_op)) {
                    temp_op->first = riscv64_convert_use_var(c, operations, temp_op->first);
                    linked_push(operations, temp_op);
                    continue;
                }

                linked_push(operations, temp_op);
            }

            continue;
        }

        if (op->code == LIR_OPCODE_RETURN) {
            linked_concat(operations, riscv64_lower_return(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_SAFEPOINT) {
            linked_concat(operations, riscv64_lower_safepoint(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_LEA) {
            linked_concat(operations, riscv64_lower_lea(c, op));
            continue;
        }

        if (lir_op_ternary(op) || op->code == LIR_OPCODE_NOT || op->code == LIR_OPCODE_NEG || lir_op_convert(op)) {
            linked_concat(operations, riscv64_lower_ternary(c, op));
            continue;
        }

        if (lir_op_contain_cmp(op)) {
            linked_concat(operations, riscv64_lower_cmp(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_MOVE && !lir_can_mov(op)) {
            op->first = riscv64_convert_use_var(c, operations, op->first);
            linked_push(operations, op);
            continue;
        }

        linked_push(operations, op);
    }

    block->operations = operations;
}

/**
 * RISC-V 64 位架构的指令降级处理入口函数
 */
void riscv64_lower(closure_t *c) {
    // 按基本块遍历所有指令
    SLICE_FOR(c->blocks) {
        basic_block_t *block = SLICE_VALUE(c->blocks);
        riscv64_lower_block(c, block);

        // 设置 block 的首尾 op
        lir_set_quick_op(block);
    }
}
