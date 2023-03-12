#include "any.h"


/**
 * 可是怎么返回超过 8byte 的数据？
 * 将 value 转换成 any 类型的数据！
 * value 必须是指针数据，因为 any 类型只能存储 8byte 的数据
 * @param rtype_index
 * @param value
 * @return
 */
any_t *any_trans(uint rtype_index, void *value) {
    // - 根据 rtype_index 找到对应的
    reflect_type_t *type = find_rtype(rtype_index);
    // TODO 是否应该在堆内存申请数据？如果需要其类型应该是 type_any_t 的 rtype 数据来申请
    // any_t 在 rtype list 中是可以预注册的，因为其 gc_bits 不会变来变去的，都是恒定不变的！
    any_t *any = NEW(any_t);
    any->type = type;
    any->value = value;

    return any;
}
