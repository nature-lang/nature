#include "basic.h"
#include "runtime/memory.h"
#include "runtime/processor.h"
#include "string.h"
#include "list.h"

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
                    type_kind_str[input_rtype->kind], \
                    type_kind_str[output_rtype->kind]); \
            exit(1); \
    }\
}

void number_casting(uint64_t input_rtype_hash, void *input_ref, uint64_t output_rtype_hash, void *output_ref) {
    rtype_t *input_rtype = rt_find_rtype(input_rtype_hash);
    rtype_t *output_rtype = rt_find_rtype(output_rtype_hash);
    DEBUGF("[convert_number] input_kind=%s, input_ref=%p, output_kind=%s, output_ref=%p",
           type_kind_str[input_rtype->kind],
           input_ref,
           type_kind_str[output_rtype->kind],
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
            assertf(false, "type %s cannot ident", type_kind_str[input_rtype->kind]);
            exit(1);
    }
}

/**
 * 如果断言异常则在 processor 中附加上错误
 * @param mu
 * @param target_rtype_hash
 * @param value_ref
 */
void union_assert(n_union_t *mu, int64_t target_rtype_hash, void *value_ref) {
    if (mu->rtype->hash != target_rtype_hash) {
        DEBUGF("[union_assert] type assert error, mu->rtype->kind: %s, target_rtype_hash: %ld",
               type_kind_str[mu->rtype->kind],
               target_rtype_hash);

        rt_processor_attach_errort("type assert error");
        return;
    }

    uint64_t size = rt_rtype_out_size(target_rtype_hash);
    memmove(value_ref, &mu->value, size);
    DEBUGF("[union_assert] success, union_base: %p, union_rtype_kind: %s, heap_out_size: %lu, union_i64_value: %ld, values_ref: %p",
           mu,
           type_kind_str[mu->rtype->kind],
           size,
           mu->value.i64_value,
           value_ref);
}

bool union_is(n_union_t *mu, int64_t target_rtype_hash) {
    return mu->rtype->hash == target_rtype_hash;
}

/**
 * @param input_rtype_hash
 * @param value
 * @return
 */
n_union_t *union_casting(uint64_t input_rtype_hash, void *value_ref) {
    // - 根据 input_rtype_hash 找到对应的
    rtype_t *rtype = rt_find_rtype(input_rtype_hash);
    assertf(rtype, "cannot find rtype, index = %lu", input_rtype_hash);

    DEBUGF("[union_casting] input_kind=%s, in_heap=%d", type_kind_str[rtype->kind], rtype->in_heap);

    rtype_t *union_rtype = gc_rtype(TYPE_UNION, 2, to_gc_kind(rtype->kind), TYPE_GC_NOSCAN);

    // any_t 在 element_rtype list 中是可以预注册的，因为其 gc_bits 不会变来变去的，都是恒定不变的！
    n_union_t *mu = runtime_malloc(sizeof(n_union_t), union_rtype);

    DEBUGF("[union_casting] union_base: %p, memmove value_ref(%p) -> any->value(%p), size=%lu, fetch_value_8byte=%p",
           mu,
           value_ref,
           &mu->value,
           rtype_out_size(rtype, POINTER_SIZE),
           (void *) fetch_addr_value((addr_t) value_ref))
    mu->rtype = rtype;

    memmove(&mu->value, value_ref, rtype_out_size(rtype, POINTER_SIZE));
    DEBUGF("[union_casting] success, union_base: %p, union_rtype: %p, union_i64_value: %ld", mu, mu->rtype,
           mu->value.i64_value);

    return mu;
}

/**
 * null/false/0 会转换成 false, 其他都是 true
 * @param input_rtype_hash
 * @param int_value
 * @return
 */
n_bool_t bool_casting(uint64_t input_rtype_hash, int64_t int_value, double float_value) {
    DEBUGF("[runtime.bool_casting] input_rtype_hash=%lu, int_value=%lu, f64_value=%f",
           input_rtype_hash,
           int_value,
           float_value);
    rtype_t *input_rtype = rt_find_rtype(input_rtype_hash);
    if (is_float(input_rtype->kind)) {
        return float_value != 0;
    }

    return int_value != 0;
}

/**
 * @param iterator
 * @param rtype_hash
 * @param cursor
 * @return
 */
int64_t iterator_next_key(void *iterator, uint64_t rtype_hash, int64_t cursor, void *key_ref) {
    DEBUGF("[runtime.iterator_next_key] iterator base=%p,rtype_hash=%lu, cursor=%ld",
           iterator, rtype_hash, cursor);

    // cursor 范围测试
    assertf(cursor >= -1 && cursor < INT32_MAX, "cursor=%ld out of range", cursor);

    rtype_t *iterator_rtype = rt_find_rtype(rtype_hash);

    cursor += 1;
    if (iterator_rtype->kind == TYPE_LIST || iterator_rtype->kind == TYPE_STRING) {
        n_list_t *list = iterator;
        DEBUGF("[runtime.iterator_next_key] list=%p, kind is list, len=%lu, cap=%lu, data_base=%p, cursor=%ld",
               list,
               list->length,
               list->capacity,
               list->data, cursor);

        if (cursor >= list->length) {
            DEBUGF("[runtime.iterator_next_key] cursor('%ld') == list.length('%ld') end", cursor, list->length);
            return -1;
        }

        DEBUGF("[runtime.iterator_next_key] list mov cursor=%ld < list.length('%ld') to key ref=%p ~ %p",
               cursor,
               list->length,
               key_ref, key_ref + INT_SIZE);

        memmove(key_ref, &cursor, INT_SIZE);
        return cursor;
    }

    if (iterator_rtype->kind == TYPE_MAP) {
        n_map_t *map = iterator;
        uint64_t key_size = rt_rtype_out_size(map->key_rtype_hash);
        DEBUGF("[runtime.iterator_next_key] kind is map, len=%lu, key_base=%p, key_index=%lu, key_size=%lu",
               map->length, map->key_data, map->key_rtype_hash, key_size);

        if (cursor >= map->length) {
            DEBUGF("[runtime.iterator_next_key] cursor('%ld') == map.length('%ld') end", cursor, map->length);
            return -1;
        }

        memmove(key_ref, map->key_data + key_size * cursor, key_size);
        return cursor;
    }

    assertf(false, "cannot support iterator type=%d", iterator_rtype->kind);
    exit(0);
}


int64_t iterator_next_value(void *iterator, uint64_t rtype_hash, int64_t cursor, void *value_ref) {
    DEBUGF("[runtime.iterator_next_value] iterator base=%p,rtype_hash=%lu, cursor=%lu",
           iterator, rtype_hash, cursor);

    rtype_t *iterator_rtype = rt_find_rtype(rtype_hash);

    cursor += 1;
    if (iterator_rtype->kind == TYPE_LIST || iterator_rtype->kind == TYPE_STRING) {
        n_list_t *list = iterator;
        assertf(list->element_rtype_hash, "list element rtype hash is empty, ptr: %p, len: %lu, cap: %lu, data: %p",
                list, list->length, list->capacity, list->data);
        uint64_t value_size = rt_rtype_out_size(list->element_rtype_hash);
        DEBUGF("[runtime.iterator_next_value] kind is list, len=%lu, cap=%lu, data_base=%p, value_size=%ld, cursor=%ld",
               list->length,
               list->capacity,
               list->data,
               value_size,
               cursor);

        if (cursor >= list->length) {
            return -1;
        }

        memmove(value_ref, list->data + value_size * cursor, value_size);
        return cursor;
    }

    if (iterator_rtype->kind == TYPE_MAP) {
        n_map_t *map = iterator;
        uint64_t value_size = rt_rtype_out_size(map->value_rtype_hash);
        DEBUGF("[runtime.iterator_next_value] kind is map, len=%lu, key_base=%p, key_index=%lu, key_size=%lu",
               map->length, map->key_data, map->key_rtype_hash, value_size);

        if (cursor >= map->length) {
            return -1;
        }

        memmove(value_ref, map->value_data + value_size * cursor, value_size);
        return cursor;
    }

    assertf(false, "cannot support iterator type=%d", iterator_rtype->kind);
    exit(0);
}

void iterator_take_value(void *iterator, uint64_t rtype_hash, int64_t cursor, void *value_ref) {
    DEBUGF("[runtime.iterator_take_value] iterator base=%p,rtype_hash=%lu, cursor=%lu, value_ref=%p",
           iterator, rtype_hash, cursor, value_ref);

    assertf(cursor != -1, "cannot iterator value");
    assertf(rtype_hash > 0, "rtype hash is empty");

    rtype_t *iterator_rtype = rt_find_rtype(rtype_hash);
    if (iterator_rtype->kind == TYPE_LIST || iterator_rtype->kind == TYPE_STRING) {
        n_list_t *list = iterator;
        DEBUGF("[runtime.iterator_take_value] kind is list, base=%p, len=%lu, cap=%lu, data_base=%p, element_hash=%lu",
               iterator,
               list->length,
               list->capacity,
               list->data,
               list->element_rtype_hash);

        assertf(list->element_rtype_hash > 0, "list element rtype hash is empty");

        assertf(cursor < list->length, "cursor=%d >= list->length=%d", cursor,
                list->length);

        uint64_t element_size = rt_rtype_out_size(list->element_rtype_hash);

        memmove(value_ref, list->data + element_size * cursor, element_size);
        DEBUGF("[runtime.iterator_take_value] iterator=%p, value_ref=%p, element_size=%lu",
               iterator, value_ref, element_size);
        return;
    }

    if (iterator_rtype->kind == TYPE_MAP) {
        n_map_t *map = iterator;
        uint64_t value_size = rt_rtype_out_size(map->value_rtype_hash);
        DEBUGF("[runtime.iterator_take_value] kind is map, len=%lu, value_base=%p, value_index=%lu, value_size=%lu",
               map->length, map->value_data, map->value_rtype_hash, value_size);

        assertf(cursor < map->length, "[runtime.iterator_take_value] cursor=%d >= map->length=%d", cursor, map->length);

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

void zero_fn() {
    rt_processor_attach_errort("zero_fn");
}

// 基于字符串到快速设置不太需要考虑内存泄漏的问题， raw_string 都是 .data 段中的字符串
void processor_throw_errort(n_string_t *msg, char *path, char *fn_name, n_int_t line, n_int_t column) {
    DEBUGF("[runtime.processor_attach_errort] msg=%s, path=%s, line=%ld, column=%ld",
           msg->data, path, line, column);
    processor_t *p = processor_get();

    n_errort *errort = n_errort_new(msg, true);

    n_trace_t trace = {
            .path = string_new(path, strlen(path)),
            .ident = string_new(fn_name, strlen(fn_name)),
            .line = line,
            .column = column,
    };
    list_push(errort->traces, &trace);

    p->errort = errort;
}

n_errort processor_remove_errort() {
    processor_t *p = processor_get();
    n_errort *errort = p->errort;
    p->errort = n_errort_new(string_new("", 0), 0);
    DEBUGF("[runtime.processor_remove_errort] remove errort: %p", errort);
    return *errort;
}

uint8_t processor_has_errort(char *path, char *fn_name, n_int_t line, n_int_t column) {
    processor_t *p = processor_get();
    DEBUGF("[runtime.processor_has_errort] errort?  %d", p->errort->has)

    if (p->errort->has) {
        // 添加栈信息
        n_trace_t trace = {
                .path = string_new(path, strlen(path)),
                .ident = string_new(fn_name, strlen(fn_name)),
                .line = line,
                .column = column,
        };

        list_push(p->errort->traces, &trace);
    }

    return p->errort->has;
}

n_cptr_t cptr_casting(value_casting v) {
    return v.u64_value;
}

n_list_t *std_args() {
    // 初始化一个 string 类型的数组
    rtype_t *list_rtype = gc_rtype(TYPE_LIST, 4, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    rtype_t *element_rtype = gc_rtype(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    n_list_t *list = list_new(list_rtype->hash, element_rtype->hash, command_argc, command_argc);

    // 初始化 string
    for (int i = 0; i < command_argc; ++i) {
        DEBUGF("[std_args] command_argv[%d]='%s'\n", i, command_argv[i]);
        n_string_t *str = string_new(command_argv[i], strlen(command_argv[i]));
        list_assign(list, i, &str);
    }

    DEBUGF("[std_args] list=%p, list->data=%p, list->length=%lu, element_rtype_hash=%lu",
           list, list->data, list->length, list->element_rtype_hash);
    return list;
}
