#include "struct.h"
#include "runtime/memory.h"

/**
 * @param rtype_hash
 * @return
 */
n_struct_t *struct_new(uint64_t rtype_hash) {


    rtype_t *rtype = rt_find_rtype(rtype_hash);
    // 参数 2 主要是去读其中的 gc_bits 记录 gc 相关数据
    return rti_gc_malloc(rtype->gc_heap_size, rtype);
}