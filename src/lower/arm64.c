#include "arm64.h"
#include "arm64_abi.h"

static lir_operand_t *arm64_convert_use_var(closure_t *c, linked_t *list, lir_operand_t *operand) {
    assert(c);
    assert(list);
    assert(operand);

    lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(operand));
    assert(temp);

    linked_push(list, lir_op_move(temp, operand));
    return lir_reset_operand(temp, operand->pos);
}

/**
 * 将 symbol 转换为 lea ptr + indirect 形式
 */
static lir_operand_t *arm64_convert_lea_symbol_var(closure_t *c, linked_t *list, lir_operand_t *symbol_var_operand) {
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

static linked_t *arm64_lower_imm(closure_t *c, lir_op_t *op) {
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

/**
 * symbol_var 需要通过 adrp 的形式寻址，所以这里进行 lea 形式改写， lea 在 native 阶段会改写成 adrp 的形式，注意此处只能基于 int 类型寄存器处理
 * 通过 lea 将符号地址加载到 int 类型寄存器, 假设是 x0 中后，后续的使用需要通过 indirect 来获取 x0 中的值
 * @param c
 * @param op
 * @return
 */
static linked_t *arm64_lower_symbol_var(closure_t *c, lir_op_t *op) {
    assert(c);
    assert(op);

    linked_t *list = linked_new();
    assert(list);

    if (op->code == LIR_OPCODE_LEA || op->code == LIR_OPCODE_LABEL) {
        return list;
    }

    if (op->first && op->first->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        op->first = arm64_convert_lea_symbol_var(c, list, op->first);
    }

    if (op->second && op->second->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        op->second = arm64_convert_lea_symbol_var(c, list, op->second);
    }

    if (op->output && op->output->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        op->output = arm64_convert_lea_symbol_var(c, list, op->output);
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
        op->first = arm64_convert_use_var(c, list, op->first);
    }

    if (op->second->assert_type != LIR_OPERAND_VAR && op->second->assert_type != LIR_OPERAND_IMM) {
        op->second = arm64_convert_use_var(c, list, op->second);
    }

    linked_push(list, op);

    // 这会导致 def 消失，可为什么 def 是 indirect addr 这也很奇怪
    if (lir_op_scc(op) && op->output->assert_type != LIR_OPERAND_VAR) {
        lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
        assert(temp);

        lir_operand_t *dst = op->output;
        op->output = lir_reset_operand(temp, op->output->pos);

        linked_push(list, lir_op_move(dst, op->output));
    }

    return list;
}

static linked_t *arm64_lower_ternary(closure_t *c, lir_op_t *op) {
    assert(op->first && op->output);

    linked_t *list = linked_new();

    // 所有的三元运算的 output 和 first 必须是 var, 这样才能分配到寄存器
    if (op->first->assert_type != LIR_OPERAND_VAR) {
        op->first = arm64_convert_use_var(c, list, op->first);
    }

    if (op->code == LIR_OPCODE_MUL || op->code == LIR_OPCODE_DIV || op->code == LIR_OPCODE_REM || op->code == LIR_OPCODE_XOR || op->code == LIR_OPCODE_OR || op->code == LIR_OPCODE_AND) {
        op->second = arm64_convert_use_var(c, list, op->second);
    }

    linked_push(list, op);

    // 如果 output 不是 var 会导致 arm64 指令异常
    if (op->output->assert_type != LIR_OPERAND_VAR) {
        lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
        assert(temp);

        lir_operand_t *dst = op->output;
        op->output = lir_reset_operand(temp, op->output->pos);

        linked_push(list, lir_op_move(dst, op->output));
    }

    return list;
}

static linked_t *arm64_lower_safepoint(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    // 创建临时寄存器存储标志地址
    lir_operand_t *flag_ptr = temp_var_operand(c->module, type_kind_new(TYPE_ANYPTR));

    // 创建符号引用
    lir_symbol_var_t *flag_symbol = NEW(lir_symbol_var_t);
    flag_symbol->kind = TYPE_BOOL;
    flag_symbol->ident = "gc_stw_safepoint";

    // lea 加载符号地址
    linked_push(list, lir_op_lea(flag_ptr, operand_new(LIR_OPERAND_SYMBOL_VAR, flag_symbol)));

    // 通过 indirect 读取实际值
    lir_operand_t *flag_value_src = indirect_addr_operand(c->module, type_kind_new(TYPE_BOOL), flag_ptr, 0);

    // 将值 move 出来，这样才能有寄存器分配出来用于 beq
    lir_operand_t *flag_value = temp_var_operand(c->module, type_kind_new(TYPE_BOOL));
    linked_push(list, lir_op_move(flag_value, flag_value_src));

    // 生成比较和跳转指令
    char *safepoint_continue_ident = label_ident_with_unique("safepoint_continue");
    lir_operand_t *stw_handler_symbol = lir_label_operand("async_preempt", false);
    linked_push(list, lir_op_new(LIR_OPCODE_BEQ, flag_value, bool_operand(false), lir_label_operand(safepoint_continue_ident, true)));

    // B
    linked_push(list, lir_op_bal(stw_handler_symbol));

    // 增加 label continue
    linked_push(list, lir_op_label(safepoint_continue_ident, true));


    return list;
}

/**
 * lea sym -> [t]
 * -->
 * lea sym -> t0
 * mov t0 -> [t] // t0 存储的已经是地址了，直接 mov 过去就行
 *
 * @param c
 * @param op
 * @return
 */
static linked_t *arm64_lower_lea(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    linked_push(list, op);

    // 这会导致 def 消失，可为什么 def 是 indirect addr 这也很奇怪
    if (op->output->assert_type != LIR_OPERAND_VAR) {
        lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
        assert(temp);

        lir_operand_t *dst = op->output;
        op->output = lir_reset_operand(temp, op->output->pos);

        linked_push(list, lir_op_move(dst, op->output));
    }

    return list;
}


static linked_t *arm64_lower_output(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();
    op->output = arm64_convert_use_var(c, list, op->output);
    linked_push(list, op);
    return list;
}

static void arm64_lower_block(closure_t *c, basic_block_t *block) {
    assert(c);
    assert(block);

    linked_t *operations = linked_new();
    LINKED_FOR(block->operations) {
        lir_op_t *op = LINKED_VALUE();

        linked_concat(operations, arm64_lower_imm(c, op));
        linked_concat(operations, arm64_lower_symbol_var(c, op));

        if (lir_op_call(op) && op->second->value != NULL) {
            linked_t *call_operations = arm64_lower_call(c, op);
            for (linked_node *call_node = call_operations->front; call_node != call_operations->rear;
                 call_node = call_node->succ) {
                lir_op_t *temp_op = call_node->value;

                linked_concat(operations, arm64_lower_symbol_var(c, temp_op));

                if (temp_op->code == LIR_OPCODE_MOVE && !lir_can_mov(temp_op)) {
                    temp_op->first = arm64_convert_use_var(c, operations, temp_op->first);
                    linked_push(operations, temp_op);
                    continue;
                }

                linked_push(operations, temp_op);
            }
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

        if (op->code == LIR_OPCODE_SAFEPOINT) {
            linked_concat(operations, arm64_lower_safepoint(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_LEA) {
            linked_concat(operations, arm64_lower_lea(c, op));
            continue;
        }

        if (lir_op_ternary(op) || op->code == LIR_OPCODE_NOT || op->code == LIR_OPCODE_NEG || lir_op_convert(op)) {
            linked_concat(operations, arm64_lower_ternary(c, op));
            continue;
        }

        if (lir_op_contain_cmp(op)) {
            linked_concat(operations, arm64_lower_cmp(c, op));
            continue;
        }

        if (op->code == LIR_OPCODE_MOVE && !lir_can_mov(op)) {
            op->first = arm64_convert_use_var(c, operations, op->first);
            linked_push(operations, op);
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
