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
            slice_free(vars);

            current = current->succ;
        }
    }
}


/**
 * Reorder non-resolve instructions (like LEA) that appear between MOVE ~ instructions 
 * before R_CALL to be placed before the first MOVE ~ in the sequence.
 * 
 * Example transformation:
 * Before:
 *   MOVE ~  REG[a] -> REG[x0]
 *   LEA     SYMBOL[foo] -> REG[x2]
 *   MOVE ~  I_ADDR[REG[x2]+0] -> REG[x1]
 *   R_CALL  ...
 * 
 * After:
 *   LEA     SYMBOL[foo] -> REG[x2]
 *   MOVE ~  REG[a] -> REG[x0]
 *   MOVE ~  I_ADDR[REG[x2]+0] -> REG[x1]
 *   R_CALL  ...
 */
static void reorder_operations(closure_t *c) {
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        linked_t *new_ops = linked_new();
        linked_node *current = linked_first(block->operations);

        while (current->value != NULL) {
            lir_op_t *op = current->value;

            // Check if we're at the start of a MOVE ~ sequence
            if (op->resolve_char == '~') {
                // Collect all operations up to and including the call
                slice_t *resolve_moves = slice_new(); // MOVE ~ ops
                slice_t *non_resolve_ops = slice_new(); // Other ops (like LEA)
                linked_node *call_node = NULL;

                // Scan forward to find R_CALL and collect all ops
                linked_node *scan = current;
                while (scan->value != NULL) {
                    lir_op_t *scan_op = scan->value;

                    if (lir_op_call(scan_op)) {
                        // Found the call - stop scanning
                        call_node = scan;
                        break;
                    }

                    if (scan_op->resolve_char == '~') {
                        slice_push(resolve_moves, scan_op);
                    } else {
                        // Non-resolve op (like LEA) that needs to be moved before resolve_moves
                        slice_push(non_resolve_ops, scan_op);
                    }

                    scan = scan->succ;
                }

                assert(call_node);
                // First push all non-resolve ops (like LEA)
                for (int j = 0; j < non_resolve_ops->count; ++j) {
                    linked_push(new_ops, non_resolve_ops->take[j]);
                }

                // Then push all resolve moves
                for (int j = 0; j < resolve_moves->count; ++j) {
                    linked_push(new_ops, resolve_moves->take[j]);
                }

                // Then push the call
                linked_push(new_ops, call_node->value);

                // Move current past the call
                current = call_node->succ;

                slice_free(resolve_moves);
                slice_free(non_resolve_ops);
                continue;
            }

            // Normal case: just push the op and move to next
            linked_push(new_ops, op);
            current = current->succ;
        }

        linked_free(block->operations);
        block->operations = new_ops;
        lir_set_quick_op(block);
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
            if (lir_op_call(op)) {
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

            // call args 标记为 '~', 将其编号为 call_op_id - 1
            // 这样所有 call args 与 call break regs 处于同一个 id 点
            if (op->resolve_char == '~') {
                op->id = next_id + 1;

                if (current->succ && lir_op_call(current->succ->value)) {
                    next_id += 2;
                }

                // resolve_char 的下一个 op 必须是 mov 或者 call
                if (current->succ) {
                    lir_op_t *succ = current->succ->value;
                    assert(lir_op_call(succ) || lir_can_mov_eliminable(succ->code));
                    if (!lir_op_call(succ)) {
                        assert(succ->resolve_char == '~');
                    }
                }

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

    reorder_operations(c);

    debug_block_lir(c, "reorder_operations");

    mark_number(c);

    debug_block_lir(c, "mark_number");

    linear_prehandle(c);

    interval_build(c);

    debug_closure_interval(c, "interval_build");

    allocate_walk(c);

    debug_closure_interval(c, "allocate_walk");

    resolve_data_flow(c);

    replace_virtual_register(c);

    debug_block_lir(c, "replace_reg");

    handle_parallel_moves(c);

    linear_posthandle(c, origin_blocks);
}
