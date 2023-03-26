#ifndef NATURE_RT_TYPE_ARRAY_H
#define NATURE_RT_TYPE_ARRAY_H

#include "utils/type.h"
#include "runtime/allocator.h"

static memory_array_t *array_new(rtype_t *element_rtype, uint64_t length) {
    // - 创建一个 typedecl_array_t 结构
    typedecl_t type_array = type_base_new(TYPE_ARRAY);
    type_array.array_decl = NEW(typedecl_array_t);
    type_array.array_decl->element_rtype = *element_rtype;
    type_array.array_decl->length = length;

    // - 将 type_array 转换成 rtype
    rtype_t rtype_array = reflect_type(type_array);

    // - 基于 rtype 进行 malloc 的申请调用, 这里进行的是堆内存申请，所以需要的就是其在在堆内存中占用的空间大小
    return runtime_malloc(rtype_array.size, &rtype_array);
}


#endif //NATURE_BASE_H
