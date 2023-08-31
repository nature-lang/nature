#include "linearscan.h"
#include "interval.h"
#include "allocate.h"
#include "amd64.h"
#include "src/debug/debug.h"

/**
 * 处理 env closure
 * @param c
 */
static void linear_posthandle(closure_t *c) {
    if (c->closure_vars == NULL) {
        return;
    }

    // 在 end_fn block 中将 stack closure 都加入进去, end_main 一定是最后一个 block
    basic_block_t *b = c->blocks->take[c->blocks->count - 1];
    linked_t *operations = linked_new();
    bool has = false;
    LINKED_FOR(b->operations) {
        lir_op_t *op = LINKED_VALUE();
        if (op->code == LIR_OPCODE_ENV_CLOSURE) {
            has = true;
            linked_concat(operations, cross_lower_env_closure(c, op));
            continue;
        }

        linked_push(operations, op);
    }
    b->operations = operations;
    assertf(has, "closure op notfound in block=%s", b->name);
}

/**
 * 1. mark number
 * 2. collect var defs
 * 3. collect capture vars
 * @param c
 */
static void linear_prehandle(closure_t *c) {
    // collect var defs
    int next_id = 0;
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

            // mark
            if (op->code == LIR_OPCODE_PHI) {
                op->id = label_op->id;
                current = current->succ;
                continue;
            }

            // capture vars
            if (op->code == LIR_OPCODE_ENV_CAPTURE) {
                assert(op->first->assert_type == LIR_OPERAND_VARS);
                slice_t *capture_vars = op->first->value;
                for (int j = 0; j < capture_vars->count; ++j) {
                    lir_var_t *var = capture_vars->take[j];
                    if (table_exist(c->closure_var_table, var->ident)) {
                        continue;
                    }
                    slice_push(c->closure_vars, var);
                    table_set(c->closure_var_table, var->ident, var);
                }
            }

            op->id = next_id;
            next_id += 2;
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
    interval_block_order(c);

    linear_prehandle(c);

    debug_block_lir(c, "mark_number");

    interval_build(c);

    debug_interval(c);

    allocate_walk(c);

    debug_interval(c);

    resolve_data_flow(c);

    replace_virtual_register(c);

    linear_posthandle(c);
}
