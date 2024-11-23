#include "arm64.h"
#include "arm64_abi.h"

static lir_operand_t *arm64_convert_mov_var(closure_t *c, linked_t *list, lir_operand_t *operand) {
    lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(operand));

    linked_push(list, lir_op_move(temp, operand));
    return lir_reset_operand(temp, operand->pos);
}

static lir_operand_t *arm64_convert_lea_var(closure_t *c, linked_t *list, lir_operand_t *operand) {
    lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(operand));

    linked_push(list, lir_op_lea(temp, operand));
    return lir_reset_operand(temp, operand->pos);
}

static linked_t *arm64_lower_imm(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
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

        }
    }

    return list;
}

static linked_t *arm64_lower_symbol_var(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    if (op->code == LIR_OPCODE_LEA || op->code == LIR_OPCODE_LABEL) {
        return list;
    }

    if (op->first && op->first->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        op->first = arm64_convert_lea_var(c, list, op->first);
    }


    if (op->second && op->second->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        op->second = arm64_convert_lea_var(c, list, op->second);
    }

    if (op->output && op->output->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        op->output = arm64_convert_lea_var(c, list, op->output);
    }

    // TODO 可能需要嵌套 symbol 处理
//    slice_t *sym_operands = extract_op_operands(op, FLAG(LIR_OPERAND_SYMBOL_VAR), 0, false);
//    for (int i = 0; i < sym_operands->count; ++i) {
//        lir_operand_t *operand = sym_operands->take[0];
//        // 添加 lea 指令将 symbol 地址添加到 var 中, 这样在寄存器分配阶段 var 必定分配到寄存器。native 阶段则可以使用 adrp + add 指令将 sym 地址添加到寄存器中
//        lir_op_lea()
//    }
    return list;
}


/**
 * 按照 arm64 规定
 * first 必须是寄存器, second 必须是寄存器或者立即数
 */
static linked_t *arm64_lower_cmp(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    if (op->first->assert_type != LIR_OPERAND_VAR) {
        op->first = arm64_convert_mov_var(c, list, op->first);
    }

    if (op->second->assert_type != LIR_OPERAND_VAR && op->second->assert_type != LIR_OPERAND_IMM) {
        op->second = arm64_convert_mov_var(c, list, op->second);
    }

    linked_push(list, op);

    return list;
}

static linked_t *arm64_lower_ternary(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    assert(op->output->assert_type == LIR_OPERAND_VAR); // var 才能分配寄存器

    // 所有的三元运算的 output 和 first 必须是 var, 这样才能分配到寄存器
    if (op->first->assert_type != LIR_OPERAND_VAR) {
        op->first = arm64_convert_mov_var(c, list, op->first);
    }

    if (op->code == LIR_OPCODE_MUL || op->code == LIR_OPCODE_DIV || op->code == LIR_OPCODE_REM) {
        op->second = arm64_convert_mov_var(c, list, op->second);
    }

    linked_push(list, op);

    return list;
}

static linked_t *arm64_lower_output(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    op->output = arm64_convert_mov_var(c, list, op->output);
    linked_push(list, op);
    return list;
}

static void arm64_lower_block(closure_t *c, basic_block_t *block) {
    linked_t *operations = linked_new();
    LINKED_FOR(block->operations) {
        lir_op_t *op = LINKED_VALUE();

        linked_concat(operations, arm64_lower_imm(c, op));
        linked_concat(operations, arm64_lower_symbol_var(c, op));

        if (lir_op_call(op) && op->second->value != NULL) {
            linked_concat(operations, arm64_lower_call(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_FN_BEGIN) {
            linked_concat(operations, arm64_lower_fn_begin(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_FN_END) {
            linked_concat(operations, arm64_lower_fn_end(c, op));
            continue;
        }

        if (is_ternary(op)) {
            linked_concat(operations, arm64_lower_ternary(c, op));
            continue;
        }

        if (lir_op_contain_cmp(op)) {
            linked_concat(operations, arm64_lower_cmp(c, op));
            continue;
        }

        linked_push(operations, op);
    }

    block->operations = operations;

}


void arm64_lower(closure_t *c) {
    // 按基本块遍历所有指令
    SLICE_FOR(c->blocks) {
        basic_block_t *block = SLICE_VALUE(c->blocks);
        arm64_lower_block(c, block);

        // 设置 block 的首尾 op
        lir_set_quick_op(block);
    }
}