#include "linearscan.h"
#include "interval.h"
#include "allocate.h"
#include "amd64.h"
#include "src/debug/debug.h"

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
void linear_scan(closure_t *c) {
    interval_block_order(c);

    interval_mark_number(c);

    debug_lir(c);

    interval_build(c);

    debug_lir(c);

    allocate_walk(c);

    resolve_data_flow(c);

    replace_virtual_register(c);
}
