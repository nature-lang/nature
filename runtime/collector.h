#ifndef NATURE_COLLECTOR_H
#define NATURE_COLLECTOR_H

#include "utils/linked.h"
#include "memory.h"


fndef_t *find_fn(addr_t addr);

/**
 * gc 入口
 * @return
 */
void runtime_gc();

#endif //NATURE_COLLECTOR_H
