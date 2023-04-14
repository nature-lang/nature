#include "basic.h"
#include "runtime/allocator.h"

memory_any_t *convert_any(uint64_t input_rtype_index, void *value) {
    // - 根据 input_rtype_index 找到对应的
    rtype_t *rtype = rt_find_rtype(input_rtype_index);

    // any_t 在 element_rtype list 中是可以预注册的，因为其 gc_bits 不会变来变去的，都是恒定不变的！
    memory_any_t *any = runtime_malloc(sizeof(memory_any_t), rtype);
    any->rtype = rtype;
    any->value = value;

    return any;
}

memory_int_t convert_int(uint64_t input_rtype_index, value_casting casting) {
    rtype_t *input_rtype = rt_find_rtype(input_rtype_index);
    if (is_integer(input_rtype->kind)) {
        int64_t v = casting.int_value;

        return (memory_int_t) v;
    }

    if (is_float(input_rtype->kind)) {
        return (memory_int_t) casting.float_value;
    }

    assertf(false, "cannot convert type=%s to int", type_kind_string[input_rtype->kind]);
    exit(0);
}

memory_float_t convert_float(uint64_t input_rtype_index, value_casting casting) {
    rtype_t *input_rtype = rt_find_rtype(input_rtype_index);
    if (is_integer(input_rtype->kind)) {
        return (memory_float_t) casting.int_value;
    }

    if (is_float(input_rtype->kind)) {
        return (memory_float_t) casting.float_value;
    }

    assertf(false, "cannot convert type=%s to float", type_kind_string[input_rtype->kind]);
    return 0;
}

