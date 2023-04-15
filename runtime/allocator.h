#ifndef NATURE_ALLOCATOR_H
#define NATURE_ALLOCATOR_H

#include "utils/slice.h"
#include "utils/linked.h"
#include "utils/helper.h"
#include "utils/bitmap.h"
#include "memory.h"


/**
 * 分配入口
 * @param size
 * @param type
 * @return
 */
void * runtime_malloc(uint64_t size, rtype_t *type);

mspan_t *mspan_new(addr_t base, uint64_t pages_count, uint8_t spanclass);

arena_hint_t *arena_hints_init();

#endif //NATURE_ALLOCATOR_H
