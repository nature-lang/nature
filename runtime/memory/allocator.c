#include "allocator.h"


/**
 * @param size
 * @param type
 * @return
 */
void *runtime_malloc(uint size, typedef_t *type) {
    // 1. 标准内存分配(0~32KB)


    // 2. 大型内存分配(大于>32KB)

    return NULL;
}


void *runtime_gc() {
    // how to get gc root?
    // 1. 全局变量? 怎么读取数据段？
    // 2. stack 数据又怎么读取？怎么遍历栈数据？怎么知道栈区的数据的大小
}