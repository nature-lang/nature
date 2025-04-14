#include "tuple.h"
#include "runtime/memory.h"

n_tuple_t *tuple_new(uint64_t rtype_hash) {


    rtype_t *rtype = rt_find_rtype(rtype_hash);
    DEBUGF("[tuple_new] rtype->size=%lu", rtype->size);
    // 参数 2 主要是去读其中的 gc_bits 记录 gc 相关数据
    return rti_gc_malloc(rtype->size, rtype);
}
