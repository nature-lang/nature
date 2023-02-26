#include "allocator.h"


/**
 * 调用 malloc 时已经将类型数据传递到了 runtime 中，obj 存储时就可以已经计算了详细的 gc_bits 能够方便的扫描出指针
 * @param size
 * @param type
 * @return
 */
void *runtime_malloc(uint size, typedef_t *type) {
    // 1. 标准内存分配(0~32KB)


    // 2. 大型内存分配(大于>32KB)

    return NULL;
}