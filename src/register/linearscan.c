#include "linearscan.h"
#include "allocate.h"
#include "interval.h"
#include "src/debug/debug.h"


/**
 * @param c
 */
static void linear_posthandle(closure_t *c, slice_t *origin_blocks) {
    c->blocks = origin_blocks;

    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        if (OP(block->last_op)->code != LIR_OPCODE_BAL) {
            continue; // fn_end
        }

        if (i == c->blocks->count - 1) {
            continue;
        }

        lir_op_t *op = OP(block->last_op);
        lir_operand_t *operand_label = op->output;
        lir_symbol_label_t *symbol_label = operand_label->value;
        char *label_ident = symbol_label->ident;

        basic_block_t *next_block = c->blocks->take[i + 1];

        if (!str_equal(label_ident, next_block->name)) {
            continue;
        }

        // 消除冗余 bal 指令
        linked_remove(block->operations, block->last_op);

        //                linked_node *current = linked_first(block->operations);
        //        lir_op_t *label_op = current->value;
        //        assert(label_op->code == LIR_OPCODE_LABEL);
        //
        //        while (current->value != NULL) {
        //            lir_op_t *op = current->value;
        //        }
    }
}

/**
 * 1. mark number
 * 2. collect var defs
 * 3. collect capture vars
 * @param c
 */
static void linear_prehandle(closure_t *c) {
    // collect var defs
    table_t *var_table = table_new();
    c->var_defs = slice_new();

    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        linked_node *current = linked_first(block->operations);
        lir_op_t *label_op = current->value;
        assert(label_op->code == LIR_OPCODE_LABEL);

        while (current->value != NULL) {
            lir_op_t *op = current->value;
            // var defs
            slice_t *vars = extract_var_operands(op, FLAG(LIR_FLAG_DEF));
            for (int j = 0; j < vars->count; ++j) {
                lir_var_t *var = vars->take[j];
                if (table_exist(var_table, var->ident)) {
                    continue;
                }

                slice_push(c->var_defs, var);
                table_set(var_table, var->ident, var);
            }

            current = current->succ;
        }
    }
}


void mark_number(closure_t *c) {
    int next_id = 0;
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        linked_node *current = linked_first(block->operations);
        lir_op_t *label_op = current->value;
        assert(label_op->code == LIR_OPCODE_LABEL);

        while (current->value != NULL) {
            lir_op_t *op = current->value;

            // check have stack operand
            if (op->code == LIR_OPCODE_CALL || op->code == LIR_OPCODE_RT_CALL) {
                c->exists_call = true;
            }

            if (op->first && op->first->assert_type == LIR_OPERAND_STACK ||
                op->second && op->second->assert_type == LIR_OPERAND_STACK ||
                op->output && op->output->assert_type == LIR_OPERAND_STACK) {
                c->exists_sp = true;
            }

            // mark
            if (op->code == LIR_OPCODE_PHI) {
                op->id = label_op->id;
                current = current->succ;
                continue;
            }

            // If label is followed by branch, then next_id is added
            op->id = next_id;
            if (op->code == LIR_OPCODE_LABEL) {
                next_id += 4;
            } else {
                next_id += 2;
            }

            current = current->succ;
        }
    }
}

/**
 * // order blocks and asm_operations (including loop detection)
 * COMPUTE_BLOCK_ORDER
 * NUMBER_OPERATIONS
 *
 * // create intervals with live ranges
 * BUILD_INTERVALS
 *
 * // allocate registers
 * WALK_INTERVALS
 *
 * INSERT_MOV between lifetime hole or spill/reload
 *
 * // in block boundary
 * RESOLVE_DATA_FLOW
 *
 * // replace virtual registers with physical registers
 * ASSIGN_REG_NUM
 * @param c
 */
void reg_alloc(closure_t *c) {
    slice_t *origin_blocks = interval_block_order(c);

    mark_number(c);

    debug_block_lir(c, "mark_number");

    linear_prehandle(c);

    interval_build(c);

    debug_closure_interval(c, "interval_build");

    allocate_walk(c);

    debug_closure_interval(c, "allocate_walk");

    resolve_data_flow(c);

    replace_virtual_register(c);

    linear_posthandle(c, origin_blocks);
}
