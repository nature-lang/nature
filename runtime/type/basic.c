#include "basic.h"
#include "runtime/allocator.h"

memory_any_t *convert_any(uint64_t rtype_index, void *value) {
    // - 根据 rtype_index 找到对应的
    rtype_t *rtype = rt_find_rtype(rtype_index);

    // any_t 在 element_rtype list 中是可以预注册的，因为其 gc_bits 不会变来变去的，都是恒定不变的！
    memory_any_t *any = runtime_malloc(sizeof(memory_any_t), rtype);
    any->rtype = rtype;
    any->value = value;

    return any;
}
