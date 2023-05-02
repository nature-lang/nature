#ifndef NATURE_RT_TYPE_ARRAY_H
#define NATURE_RT_TYPE_ARRAY_H

#include "utils/type.h"
#include "runtime/allocator.h"

static inline memory_array_t *array_new(rtype_t *element_rtype, uint64_t length) {
    // - 创建一个 typeuse_array_t 结构
    type_t type_array = type_basic_new(TYPE_ARRAY);
    type_array.array = NEW(type_array_t);
    type_array.array->element_rtype = *element_rtype;
    type_array.array->length = length;

    // - 将 type_array 转换成 rtype
    rtype_t rtype = rtype_array(type_array.array);

    // - 基于 rtype 进行 malloc 的申请调用, 这里进行的是堆内存申请，所以需要的就是其在在堆内存中占用的空间大小
    void *addr = runtime_malloc(rtype.size, &rtype);
    DEBUGF("[array_new] success, base=%p, element_rtype.size=%lu, element_rtype.kind=%s, array_rtype_size=%lu(length=%lu),rtype_kind=%s",
           addr,
           element_rtype->size,
           type_kind_string[element_rtype->kind],
           rtype.size,
           length,
           type_kind_string[rtype.kind])

    return addr;
}


#endif //NATURE_BASE_H
