#include "any.h"
#include "runtime/allocator.h"


/**
 * 可是怎么返回超过 8byte 的数据？
 * 将 value 转换成 any 类型的数据！
 * value 必须是指针数据，因为 any 类型只能存储 8byte 的数据
 * @param rtype_index
 * @param value
 * @return
 */
memory_any_t *trans_any(uint rtype_index, void *value) {
    // - 根据 rtype_index 找到对应的
    reflect_type_t *rtype = find_rtype(rtype_index);

    // any_t 在 rtype list 中是可以预注册的，因为其 gc_bits 不会变来变去的，都是恒定不变的！
    memory_any_t *any = (void *) runtime_malloc(sizeof(memory_any_t), rtype);
    any->rtype = rtype;
    any->value = value;

    return any;
}
