#ifndef NATURE_COLLECTOR_H
#define NATURE_COLLECTOR_H

#include "utils/list.h"

typedef struct {
    /**
     * mark root 会将所有的
     */
    list *grey_list;
} collector_t;


/**
 * gc 入口
 * @return
 */
void *runtime_gc();

#endif //NATURE_COLLECTOR_H
