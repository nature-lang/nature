#ifndef NATURE_RT_TYPE_ARRAY_H
#define NATURE_RT_TYPE_ARRAY_H

#include "nutils.h"
#include "runtime/memory.h"
#include "utils/type.h"

static inline n_array_t *rt_array_new(rtype_t *element_rtype, uint64_t length) {
    assert(element_rtype && "element_rtype is null");
    assert(element_rtype->size > 0 && "element_rtype size is zero");

    SAFE_DEBUGF("[rt_array_new] element_rtype.size=%lu(outsize: %lu), element_rtype.kind=%s(need_gc=%d), length=%lu", element_rtype->size,
                rtype_out_size(element_rtype, POINTER_SIZE), type_kind_str[element_rtype->kind], element_rtype->last_ptr > 0, length);

    // - 创建一个 typeuse_array_t 结构
    rtype_t rtype = rt_rtype_array(element_rtype, length);

    SAFE_DEBUGF("[rt_array_new] rt_rtype_array success");

    // - 基于 rtype 进行 malloc 的申请调用, 这里进行的是堆内存申请，所以需要的就是其在在堆内存中占用的空间大小
    void *addr = runtime_zero_malloc(rtype.size, &rtype);
    SAFE_DEBUGF(
        "[rt_array_new] success, base=%p, element_rtype.size=%lu, element_rtype.kind=%s(need_gc=%d), "
        "array_rtype_size=%lu(length=%lu),rtype_kind=%s",
        addr, element_rtype->size, type_kind_str[element_rtype->kind], element_rtype->last_ptr > 0, rtype.size, length,
        type_kind_str[rtype.kind]);

    safe_free(rtype.gc_bits);

    return addr;
}

n_cptr_t array_element_addr(n_array_t *data, uint64_t rtype_hash, uint64_t index);

#endif // NATURE_BASE_H
