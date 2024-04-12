#ifndef NATURE_RT_TYPE_ARRAY_H
#define NATURE_RT_TYPE_ARRAY_H

#include "nutils.h"
#include "runtime/memory.h"
#include "utils/type.h"

static inline n_array_t *rt_array_new(rtype_t *element_rtype, uint64_t length) {
    assert(element_rtype && "element_rtype is null");
    assert(element_rtype->size > 0 && "element_rtype size is zero");

    TRACEF("[rt_array_new] ele_sz=%lu(rtype_out_size=%lu),ele_kind=%s(n_gc=%d),len=%lu", element_rtype->size,
           rtype_out_size(element_rtype, POINTER_SIZE), type_kind_str[element_rtype->kind], element_rtype->last_ptr > 0, length);

    // - 创建一个 typeuse_array_t 结构
    rtype_t rtype = rt_rtype_array(element_rtype, length);

    TRACEF("[rt_array_new] rt_rtype_array success");

    // - 基于 rtype 进行 malloc 的申请调用, 这里进行的是堆内存申请，所以需要的就是其在在堆内存中占用的空间大小
    void *addr = rt_clr_malloc(rtype.size, &rtype);
    TRACEF(
        "[rt_array_new] success, base=%p, element_rtype.size=%lu, element_rtype.kind=%s(need_gc=%d), "
        "array_rtype_size=%lu(length=%lu),rtype_kind=%s",
        addr, element_rtype->size, type_kind_str[element_rtype->kind], element_rtype->last_ptr > 0, rtype.size, length,
        type_kind_str[rtype.kind]);

    free(rtype.gc_bits);

    return addr;
}

n_void_ptr_t array_element_addr(n_array_t *data, uint64_t rtype_hash, uint64_t index);

#endif // NATURE_BASE_H
