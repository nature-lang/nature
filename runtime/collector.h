#ifndef NATURE_COLLECTOR_H
#define NATURE_COLLECTOR_H

#include "utils/list.h"
#include "memory.h"


/**
 * gc 入口
 * @return
 */
void *runtime_gc();

void sweep_mcentral(mheap_t *mheap);

#endif //NATURE_COLLECTOR_H
