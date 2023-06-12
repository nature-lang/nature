#include "basic.h"
#include "runtime/memory.h"
#include "processor.h"
#include "string.h"

#define _NUMBER_CASTING(_kind, _input_value, _debug_int64_value) { \
    switch (_kind) { \
        case TYPE_FLOAT: \
        case TYPE_FLOAT64: \
            *(double *) output_ref = (double) _input_value; \
            return; \
        case TYPE_FLOAT32: \
            *(float *) output_ref = (float) _input_value; \
            return; \
        case TYPE_INT: \
        case TYPE_INT64: \
            *(int64_t *) output_ref = (int64_t) _input_value; \
            return; \
        case TYPE_INT32: \
            *(int32_t *) output_ref = (int32_t) _input_value; \
            return; \
        case TYPE_INT16:                       \
            *(int16_t *) output_ref = (int16_t) _input_value; \
            DEBUGF("[runtime.number_casting] output(i16): %d, debug_input(i64): %ld", *(int16_t*)output_ref, _debug_int64_value); \
            return;                            \
        case TYPE_INT8: \
            *(int8_t *) output_ref = (int8_t) _input_value; \
            return; \
        case TYPE_UINT: \
        case TYPE_UINT64: \
            *(uint64_t *) output_ref = (uint64_t) _input_value; \
            return; \
        case TYPE_UINT32: \
            *(uint32_t *) output_ref = (uint32_t) _input_value; \
            return; \
        case TYPE_UINT16: \
            *(uint16_t *) output_ref = (uint16_t) _input_value; \
            return; \
        case TYPE_UINT8: \
            *(uint8_t *) output_ref = (uint8_t) _input_value; \
            return; \
        default: \
            assertf(false, "cannot convert %s to %s type", \
                    type_kind_string[input_rtype->kind], \
                    type_kind_string[output_rtype->kind]); \
            exit(1); \
    }\
}

void number_casting(uint64_t input_rtype_index, void *input_ref, uint64_t output_rtype_index, void *output_ref) {
    rtype_t *input_rtype = rt_find_rtype(input_rtype_index);
    rtype_t *output_rtype = rt_find_rtype(output_rtype_index);
    DEBUGF("[convert_number] input_kind=%s, input_ref=%p, output_kind=%s, output_ref=%p",
           type_kind_string[input_rtype->kind],
           input_ref,
           type_kind_string[output_rtype->kind],
           output_ref);

    value_casting v = {0};
    memmove(&v, input_ref, input_rtype->size);

    switch (input_rtype->kind) {
        case TYPE_FLOAT:
        case TYPE_FLOAT64: _NUMBER_CASTING(output_rtype->kind, v.f64_value, v.i64_value);
        case TYPE_FLOAT32: _NUMBER_CASTING(output_rtype->kind, v.f32_value, v.i64_value);
        case TYPE_INT:
        case TYPE_INT64: _NUMBER_CASTING(output_rtype->kind, v.i64_value, v.i64_value);
        case TYPE_INT32: _NUMBER_CASTING(output_rtype->kind, v.i32_value, v.i64_value);
        case TYPE_INT16: _NUMBER_CASTING(output_rtype->kind, v.i16_value, v.i64_value);
        case TYPE_INT8: _NUMBER_CASTING(output_rtype->kind, v.i8_value, v.i64_value);
        case TYPE_UINT:
        case TYPE_UINT64: _NUMBER_CASTING(output_rtype->kind, v.u64_value, v.i64_value);
        case TYPE_UINT32: _NUMBER_CASTING(output_rtype->kind, v.u32_value, v.i64_value);
        case TYPE_UINT16: _NUMBER_CASTING(output_rtype->kind, v.u16_value, v.i64_value);
        case TYPE_UINT8: _NUMBER_CASTING(output_rtype->kind, v.u8_value, v.i64_value);
        default:
            assertf(false, "type %s cannot ident", type_kind_string[input_rtype->kind]);
            exit(1);
    }
}

/**
 * 如果断言异常则在 processor 中附加上错误
 * @param mu
 * @param target_rtype_index
 * @param value_ref
 */
void union_assert(memory_union_t *mu, int64_t target_rtype_index, void *value_ref) {
    if (mu->rtype->index != target_rtype_index) {
        rt_processor_attach_errort("type assert error");
        return;
    }

    uint64_t size = rt_rtype_heap_out_size(target_rtype_index);

    memmove(value_ref, &mu->value, size);
    DEBUGF("[union_assert] success, union_base: %p, union_rtype: %p, union_i64_value: %ld", mu, mu->rtype,
           mu->value.i64_value);
}

bool union_is(memory_union_t *mu, int64_t target_rtype_index) {
    return mu->rtype->index == target_rtype_index;
}

/**
 * @param input_rtype_index
 * @param value
 * @return
 */
memory_union_t *union_casting(uint64_t input_rtype_index, void *value_ref) {
    // - 根据 input_rtype_index 找到对应的
    rtype_t *rtype = rt_find_rtype(input_rtype_index);
    assertf(rtype, "cannot find rtype, index = %lu", input_rtype_index);

    DEBUGF("[union_casting] input_kind=%s, in_heap=%d", type_kind_string[rtype->kind], rtype->in_heap);

    rtype_t union_rtype = gc_rtype(TYPE_UNION, 2, to_gc_kind(rtype->kind), TYPE_GC_NOSCAN);

    // any_t 在 element_rtype list 中是可以预注册的，因为其 gc_bits 不会变来变去的，都是恒定不变的！
    memory_union_t *mu = runtime_malloc(sizeof(memory_union_t), &union_rtype);

    DEBUGF("[union_casting] union_base: %p, memmove value_ref(%p) -> any->value(%p), size=%lu, fetch_value_8byte=%p",
           mu,
           value_ref,
           &mu->value,
           rtype_heap_out_size(rtype, POINTER_SIZE),
           (void *) fetch_addr_value((addr_t) value_ref))
    mu->rtype = rtype;

    memmove(&mu->value, value_ref, rtype_heap_out_size(rtype, POINTER_SIZE));
    DEBUGF("[union_casting] success, union_base: %p, union_rtype: %p, union_i64_value: %ld", mu, mu->rtype,
           mu->value.i64_value);

    return mu;
}

/**
 * null/false/0 会转换成 false, 其他都是 true
 * @param input_rtype_index
 * @param int_value
 * @return
 */
memory_bool_t bool_casting(uint64_t input_rtype_index, int64_t int_value, double float_value) {
    DEBUGF("[runtime.bool_casting] input_rtype_index=%lu, int_value=%lu, f64_value=%f",
           input_rtype_index,
           int_value,
           float_value);
    rtype_t *input_rtype = rt_find_rtype(input_rtype_index);
    if (is_float(input_rtype->kind)) {
        return float_value != 0;
    }

    return int_value != 0;
}

/**
 * @param iterator
 * @param rtype_index
 * @param cursor
 * @return
 */
int64_t iterator_next_key(void *iterator, uint64_t rtype_index, int64_t cursor, void *key_ref) {
    DEBUGF("[runtime.iterator_next_key] iterator base=%p,rtype_index=%lu, cursor=%lu",
           iterator, rtype_index, cursor);

    rtype_t *iterator_rtype = rt_find_rtype(rtype_index);

    cursor += 1;
    if (iterator_rtype->kind == TYPE_LIST) {
        memory_list_t *list = iterator;
        DEBUGF("[runtime.iterator_next_key] kind is list, len=%lu, cap=%lu, data_base=%p", list->length, list->capacity,
               list->array_data);

        if (cursor >= list->length) {
            return -1;
        }

        memmove(key_ref, &cursor, INT_SIZE);
        return cursor;
    }

    if (iterator_rtype->kind == TYPE_MAP) {
        memory_map_t *map = iterator;
        uint64_t key_size = rt_rtype_heap_out_size(map->key_index);
        DEBUGF("[runtime.iterator_next_key] kind is map, len=%lu, key_base=%p, key_index=%lu, key_size=%lu",
               map->length, map->key_data, map->key_index, key_size);

        if (cursor >= map->length) {
            return -1;
        }

        memmove(key_ref, map->key_data + key_size * cursor, key_size);
        return cursor;
    }

    assertf(false, "cannot support iterator type=%d", iterator_rtype->kind);
    exit(0);
}


void iterator_value(void *iterator, uint64_t rtype_index, int64_t cursor, void *value_ref) {
    DEBUGF("[runtime.iterator_next_value] iterator base=%p,rtype_index=%lu, cursor=%lu",
           iterator, rtype_index, cursor);
    assertf(cursor != -1, "cannot iterator value");

    rtype_t *iterator_rtype = rt_find_rtype(rtype_index);

    if (iterator_rtype->kind == TYPE_LIST) {
        memory_list_t *list = iterator;
        DEBUGF("[runtime.iterator_key] kind is list, len=%lu, cap=%lu, data_base=%p", list->length, list->capacity,
               list->array_data);

        assertf(cursor < list->length, "[runtime.iterator_value] cursor=%d >= list->length=%d", cursor, list->length);

        uint64_t element_size = rt_rtype_heap_out_size(list->element_rtype_index);
        memmove(value_ref, list->array_data + element_size * cursor, element_size);
        return;
    }

    if (iterator_rtype->kind == TYPE_MAP) {
        memory_map_t *map = iterator;
        uint64_t value_size = rt_rtype_heap_out_size(map->value_index);
        DEBUGF("[runtime.iterator_key] kind is map, len=%lu, value_base=%p, value_index=%lu, value_size=%lu",
               map->length, map->value_data, map->value_index, value_size);

        assertf(cursor < map->length, "[runtime.iterator_value] cursor=%d >= map->length=%d", cursor, map->length);

        memmove(value_ref, map->value_data + value_size * cursor, value_size);
        return;
    }

    assertf(false, "cannot support iterator type=%d", iterator_rtype->kind);
    exit(0);
}


/**
 * dst offset 按字节记
 * @param dst
 * @param dst_offset
 * @param src
 * @param size
 */
void memory_move(uint8_t *dst, uint64_t dst_offset, void *src, uint64_t src_offset, uint64_t size) {
    DEBUGF("[runtime.memory_move] dst=%p, dst_offset=%lu, src=%p, src_offset=%lu, size=%lu",
           dst, dst_offset, src, src_offset, size);
    memmove(dst + dst_offset, src + src_offset, size);
}