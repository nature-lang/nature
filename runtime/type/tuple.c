#include "tuple.h"
#include "runtime/memory.h"
#include "runtime/allocator.h"

memory_tuple_t *tuple_new(uint64_t rtype_index) {
    rtype_t *rtype = rt_find_rtype(rtype_index);
    // 参数 2 主要是去读其中的 gc_bits 记录 gc 相关数据
    return runtime_malloc(rtype->size, rtype);
}
