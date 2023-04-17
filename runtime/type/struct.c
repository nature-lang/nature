#include "struct.h"
#include "utils/type.h"
#include "utils/custom_links.h"
#include "runtime/memory.h"
#include "runtime/allocator.h"

/**
 * @param rtype_index
 * @return
 */
memory_struct_t *struct_new(uint64_t rtype_index) {
    rtype_t *rtype = rt_find_rtype(rtype_index);
    // 参数 2 主要是去读其中的 gc_bits 记录 gc 相关数据
    return runtime_malloc(rtype->size, rtype);
}

/**
 * rtype 中也没有保存 struct 每个 key 的 offset, 所以需要编译时计算好 offset 传过来
 * access 操作可以直接解析成 lir_indrect_addr_t, 没必要再调用这里计算地址了
 * @param s
 * @return
 */
void *struct_access(memory_struct_t *s, uint64_t offset) {
    return s + offset;
}

void struct_assign(memory_struct_t *s, uint64_t offset, uint64_t property_index, void *property_ref) {
    void *p = s + offset;
    uint64_t size = rtype_heap_out_size(rt_find_rtype(property_index), POINTER_SIZE);
    memmove(p, property_ref, size);
}


