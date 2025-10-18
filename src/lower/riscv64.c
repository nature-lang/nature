#include "riscv64.h"
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

static linked_t *riscv64_lower_imm(closure_t *c, lir_op_t *op) {
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

        if (imm->kind == TYPE_RAW_STRING || is_float(imm->kind)) {
            char *unique_name = var_unique_ident(c->module, TEMP_VAR_IDENT);
            assert(unique_name);

            asm_global_symbol_t *symbol = NEW(asm_global_symbol_t);
            assert(symbol);
            symbol->name = unique_name;

            if (imm->kind == TYPE_RAW_STRING) {
                assert(imm->string_value);
                symbol->size = imm->strlen + 1;
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
                symbol_table_set_raw_string(c->module, unique_name, type_kind_new(TYPE_RAW_STRING), imm->strlen);

                // raw_string 本身就是指针类型, 首次加载时需要通过 la 将数据段地址加载到 var_operand
                lir_operand_t *var_operand = temp_var_operand(c->module, type_kind_new(TYPE_RAW_STRING));
                lir_op_t *temp_ref = lir_op_lea(var_operand, operand_new(LIR_OPERAND_SYMBOL_VAR, symbol_var));
                linked_push(list, temp_ref);

                lir_operand_t *temp_operand = lir_reset_operand(var_operand, imm_operand->pos);
                imm_operand->assert_type = temp_operand->assert_type;
                imm_operand->value = temp_operand->value;
            } else {
                // 浮点数可以通过全局符号表访问
                imm_operand->assert_type = LIR_OPERAND_SYMBOL_VAR;
                imm_operand->value = symbol_var;
            }
        }
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
    if (op->code == LIR_OPCODE_BEQ) {
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

    // 创建临时寄存器存储标志地址
    lir_operand_t *result_reg = lir_reg_operand(A0->index, TYPE_ANYPTR);
    op->output = result_reg;

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

        linked_concat(operations, riscv64_lower_imm(c, op));
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

        if (op->code == LIR_OPCODE_FN_END) {
            linked_concat(operations, riscv64_lower_fn_end(c, op));
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
