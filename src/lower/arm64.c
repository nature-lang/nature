#include "arm64.h"
#include "arm64_abi.h"
#include "lower.h"
#include "src/binary/encoding/arm64/fmov_imm8.h"

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

static linked_t *arm64_lower_imm(closure_t *c, lir_op_t *op, linked_t *symbol_operations) {
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

        // arm64 浮点 const 特殊优化, 不需要借助 global symbol， 直接基于 closure table 即可
        if (is_float(imm->kind) && arm64_fmov_double_to_imm8(imm->f64_value) != 0xFF) {
            char *key = itoa(imm->int_value);

            lir_operand_t *local_var_operand = table_get(c->local_imm_table, key);
            if (local_var_operand == NULL) {
                local_var_operand = temp_var_operand(c->module, type_kind_new(imm->kind));
                imm->uint_value = arm64_fmov_double_to_imm8(imm->f64_value);
                lir_operand_t *new_imm_operand = lir_reset_operand(imm_operand, LIR_FLAG_FIRST);

                lir_var_t *var = local_var_operand->value; // def
                var->flag |= FLAG(LIR_FLAG_CONST);
                var->imm_value.uint_value = imm->uint_value; // origin value

                // 创建 remat_ops 模板 (FMOV imm8)
                linked_t *remat_ops = linked_new();
                linked_push(remat_ops, lir_op_move(local_var_operand, new_imm_operand));
                var->remat_ops = remat_ops;

                // 虚拟模板指令，后续会被直接 spill 删除
                linked_push(symbol_operations, lir_op_nop_def(local_var_operand));


                table_set(c->local_imm_table, key, local_var_operand);
            }

            // change imm to local var
            lir_operand_t *temp_operand = lir_reset_operand(local_var_operand, imm_operand->pos);
            lir_var_t *var = temp_operand->value;
            var->flag |= FLAG(LIR_FLAG_CONST);
            var->imm_value.uint_value = imm->uint_value;

            imm_operand->assert_type = temp_operand->assert_type;
            imm_operand->value = temp_operand->value;
            continue;
        }

        lower_imm_symbol(c, imm_operand, list, symbol_operations);
    }

    return list;
}

/**
 * symbol_var 需要通过 adrp 的形式寻址，所以这里进行 lea 形式改写， lea 在 native 阶段会改写成 adrp 的形式，注意此处只能基于 int 类型寄存器处理
 * 通过 lea 将符号地址加载到 int 类型寄存器, 假设是 x0 中后，后续的使用需要通过 indirect 来获取 x0 中的值
 *
 * 所有的 symbol_var 都会被改造成 lea 指令的形式
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

    if (lir_op_branch_cmp(op)) {
        if (op->first && op->first->assert_type == LIR_OPERAND_SYMBOL_VAR) {
            op->first = arm64_convert_lea_symbol_var(c, list, op->first);
        }

        if (op->second && op->second->assert_type == LIR_OPERAND_SYMBOL_VAR) {
            op->second = arm64_convert_lea_symbol_var(c, list, op->second);
        }
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

    if ((op->code == LIR_OPCODE_MUL ||
         op->code == LIR_OPCODE_UDIV ||
         op->code == LIR_OPCODE_UREM ||
         op->code == LIR_OPCODE_SDIV ||
         op->code == LIR_OPCODE_SREM ||
         op->code == LIR_OPCODE_XOR ||
         op->code == LIR_OPCODE_OR ||
         op->code == LIR_OPCODE_AND) &&
        op->second->assert_type != LIR_OPERAND_VAR) {
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
    //    lir_operand_t *result_reg;
    //    if (BUILD_OS == OS_DARWIN) {
    //        result_reg = lir_reg_operand(x0->index, TYPE_ANYPTR);
    //    } else {
    //        result_reg = lir_reg_operand(x28->index, TYPE_ANYPTR);
    //    }
    //    op->output = result_reg;

    // 增加 label continue
    linked_push(list, op);


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

/**
 * Lower FMA instructions (MADD/MSUB/FMADD/FMSUB)
 * 将 first, second, addend 操作数转换为 VAR，从而让寄存器分配能够分配寄存器
 *
 * FMA instruction format:
 *   first: mul_first (Rn)
 *   second: mul_second (Rm)
 *   addend: addend/minuend (Ra)
 *   output: result (Rd)
 *
 * @param c
 * @param op
 * @return
 */
static linked_t *arm64_lower_fma(closure_t *c, lir_op_t *op) {
    linked_t *list = linked_new();

    // first 必须是 VAR
    if (op->first->assert_type != LIR_OPERAND_VAR) {
        op->first = arm64_convert_use_var(c, list, op->first);
    }

    // second 必须是 VAR
    if (op->second->assert_type != LIR_OPERAND_VAR) {
        op->second = arm64_convert_use_var(c, list, op->second);
    }

    // addend 必须是 VAR
    if (op->addend->assert_type != LIR_OPERAND_VAR) {
        op->addend = arm64_convert_use_var(c, list, op->addend);
    }

    linked_push(list, op);

    // 如果 output 不是 VAR 则需要额外的 MOV
    if (op->output->assert_type != LIR_OPERAND_VAR) {
        lir_operand_t *temp = temp_var_operand_with_alloc(c->module, lir_operand_type(op->output));
        assert(temp);

        lir_operand_t *dst = op->output;
        op->output = lir_reset_operand(temp, op->output->pos);

        linked_push(list, lir_op_move(dst, op->output));
    }

    return list;
}

static void arm64_lower_block(closure_t *c, basic_block_t *block) {
    assert(c);
    assert(block);

    linked_t *operations = linked_new();
    LINKED_FOR(block->operations) {
        lir_op_t *op = LINKED_VALUE();

        linked_t *symbol_operations = linked_new();
        linked_concat(operations, arm64_lower_imm(c, op, symbol_operations));

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
            linked_t *fn_begin_operations = arm64_lower_fn_begin(c, op);
            for (linked_node *fn_begin_node = fn_begin_operations->front; fn_begin_node != fn_begin_operations->rear;
                 fn_begin_node = fn_begin_node->succ) {
                lir_op_t *temp_op = fn_begin_node->value;
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

        if (op->code == LIR_OPCODE_RETURN) {
            linked_concat(operations, arm64_lower_return(c, op));
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

        // FMA 指令处理 (MADD/MSUB/FMADD/FMSUB)
        if (op->code == LIR_OPCODE_MADD || op->code == LIR_OPCODE_MSUB ||
            op->code == LIR_OPCODE_FMADD || op->code == LIR_OPCODE_FMSUB) {
            linked_concat(operations, arm64_lower_fma(c, op));
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
