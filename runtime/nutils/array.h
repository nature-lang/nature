#ifndef NATURE_RT_TYPE_ARRAY_H
#define NATURE_RT_TYPE_ARRAY_H

#include "nutils.h"
#include "runtime/memory.h"
#include "utils/type.h"
#include "runtime/rtype.h"

static inline n_array_t *rti_array_new(rtype_t *element_rtype, uint64_t length) {
    assert(element_rtype && "element_rtype is null");
    assert(element_rtype->size > 0 && "element_rtype size is zero");

    TRACEF("[rti_array_new] ele_sz=%lu(rtype_stack_size=%lu),ele_kind=%s(n_gc=%d),len=%lu", element_rtype->size,
           rtype_stack_size(element_rtype, POINTER_SIZE), type_kind_str[element_rtype->kind],
           element_rtype->last_ptr > 0,
           length);

    rtype_t rtype = rti_rtype_array(element_rtype, length);
    TRACEF("[rti_array_new] rti_rtype_array success");

    // - 基于 rtype 进行 malloc 的申请调用, 这里进行的是堆内存申请，所以需要的就是其在在堆内存中占用的空间大小
    void *addr = rti_gc_malloc(rtype.size, &rtype);
    TRACEF(
        "[rti_array_new] success, base=%p, element_rtype.size=%lu, element_rtype.kind=%s(need_gc=%d), "
        "array_rtype_size=%lu(length=%lu),rtype_kind=%s",
        addr, element_rtype->size, type_kind_str[element_rtype->kind], element_rtype->last_ptr > 0, rtype.size,
        length,
        type_kind_str[rtype.kind]);

    return addr;
}

#endif // NATURE_BASE_H
