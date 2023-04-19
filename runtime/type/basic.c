#include "basic.h"
#include "runtime/allocator.h"

/**
 * TODO value 可能是各个角度传递过来的实际的值, 比如 int 就是 int
 * 但是当传递当类型为 float 时，由于 float 走 xmm0 寄存器，所以会有读取当问题
 * @param input_rtype_index
 * @param value
 * @return
 */
memory_any_t *convert_any(uint64_t input_rtype_index, void *value_ref) {
    // - 根据 input_rtype_index 找到对应的
    rtype_t *rtype = rt_find_rtype(input_rtype_index);

    assertf(rtype, "cannot find rtype, index = %lu", input_rtype_index);

    DEBUGF("[convert_any] input_rtype_index=%lu, kind=%d, actual_index=%lu, in_heap=%d",
           input_rtype_index, rtype->kind, rtype->index, rtype->in_heap);

    rtype_t any_rtype = rt_reflect_type(type_basic_new(TYPE_ANY));

    // any_t 在 element_rtype list 中是可以预注册的，因为其 gc_bits 不会变来变去的，都是恒定不变的！
    memory_any_t *any = runtime_malloc(sizeof(memory_any_t), &any_rtype);

    DEBUGF("[convert_any] any_base: %p, memmove value_ref(%p) -> any->value(%p), size=%lu, fetch_value_8byte=%p",
           any,
           value_ref,
           &any->value,
           rtype_heap_out_size(rtype, POINTER_SIZE),
           (void *) fetch_addr_value((addr_t) value_ref))

    any->rtype = rtype;
    memmove(&any->value, value_ref, rtype_heap_out_size(rtype, POINTER_SIZE));
    DEBUGF("[convert_any] success")

    return any;
}

memory_int_t convert_int(uint64_t input_rtype_index, value_casting casting) {
    DEBUGF("[convert_int] input_index=%lu, int_v=%lu, float_v=%f",
           input_rtype_index,
           casting.int_value,
           casting.float_value);

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
    DEBUGF("[convert_float] input_index=%lu, int_v=%lu, float_v=%f",
           input_rtype_index,
           casting.int_value,
           casting.float_value);

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

uint64_t var_clr_def() {
    return 0;
}

/**
 * null/false/0 会转换成 false, 其他都是 true
 * @param input_rtype_index
 * @param casting
 * @return
 */
memory_bool_t convert_bool(uint64_t input_rtype_index, value_casting casting) {
    return casting.bool_value;
}

