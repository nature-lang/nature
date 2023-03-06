#ifndef NATURE_ALLOCATOR_H
#define NATURE_ALLOCATOR_H

#include "utils/slice.h"
#include "utils/list.h"
#include "utils/helper.h"
#include "utils/bitmap.h"
#include "utils/links/typedef.h"
#include "memory.h"

/**
 * @param mheap
 * 遍历 processor_list 将所有的 mcache 中持有的 mspan 都 push 到对应的 mcentral 中
 */
void flush_mcache();

/**
 * 分配入口
 * @param size
 * @param type
 * @return
 */
void *runtime_malloc(uint size, typedef_t *type);


arena_t *arena_new(mheap_t mheap);

arena_hint_t *arena_hints_init();

#endif //NATURE_ALLOCATOR_H
