#include "linearscan.h"
#include "interval.h"
#include "allocate.h"

/**
 * // order blocks and operations (including loop detection)
 * COMPUTE_BLOCK_ORDER
 * NUMBER_OPERATIONS
 *
 * // create intervals with live ranges
 * COMPUTE_LOCAL_LIVE_SETS
 * COMPUTE_GLOBAL_LIVE_SETS
 * BUILD_INTERVALS
 *
 * // allocate registers
 * WALK_INTERVALS
 * RESOLVE_DATA_FLOW
 * // replace virtual registers with physical registers
 * ASSIGN_REG_NUM
 * @param c
 */
void linear_scan(closure *c) {

}

