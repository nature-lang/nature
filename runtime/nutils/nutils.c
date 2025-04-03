#include "nutils.h"

#include "array.h"
#include "errort.h"
#include "runtime/memory.h"
#include "runtime/processor.h"
#include "runtime/rt_chan.h"
#include "runtime/rtype.h"
#include "string.h"
#include "vec.h"

int command_argc;
char **command_argv;


#define _NUMBER_CASTING(_kind, _input_value, _debug_int64_value)                                                                        \
    {                                                                                                                                   \
        switch (_kind) {                                                                                                                \
            case TYPE_FLOAT:                                                                                                            \
            case TYPE_FLOAT64:                                                                                                          \
                *(double *) output_ref = (double) _input_value;                                                                         \
                return;                                                                                                                 \
            case TYPE_FLOAT32:                                                                                                          \
                *(float *) output_ref = (float) _input_value;                                                                           \
                return;                                                                                                                 \
            case TYPE_INT:                                                                                                              \
            case TYPE_INT64:                                                                                                            \
                *(int64_t *) output_ref = (int64_t) _input_value;                                                                       \
                return;                                                                                                                 \
            case TYPE_INT32:                                                                                                            \
                *(int32_t *) output_ref = (int32_t) _input_value;                                                                       \
                return;                                                                                                                 \
            case TYPE_INT16:                                                                                                            \
                *(int16_t *) output_ref = (int16_t) _input_value;                                                                       \
                DEBUGF("[runtime.number_casting] output(i16): %d, debug_input(i64): %ld", *(int16_t *) output_ref, _debug_int64_value); \
                return;                                                                                                                 \
            case TYPE_INT8:                                                                                                             \
                *(int8_t *) output_ref = (int8_t) _input_value;                                                                         \
                return;                                                                                                                 \
            case TYPE_UINT:                                                                                                             \
            case TYPE_UINT64:                                                                                                           \
                *(uint64_t *) output_ref = (uint64_t) (int64_t) _input_value;                                                           \
                return;                                                                                                                 \
            case TYPE_UINT32:                                                                                                           \
                *(uint32_t *) output_ref = (uint32_t) (int32_t) _input_value;                                                           \
                return;                                                                                                                 \
            case TYPE_UINT16:                                                                                                           \
                *(uint16_t *) output_ref = (uint16_t) (int16_t) _input_value;                                                           \
                return;                                                                                                                 \
            case TYPE_UINT8:                                                                                                            \
                *(uint8_t *) output_ref = (uint8_t) (int8_t) _input_value;                                                              \
                return;                                                                                                                 \
            default:                                                                                                                    \
                assert(false && "cannot scanner_number_convert type");                                                                  \
                exit(1);                                                                                                                \
        }                                                                                                                               \
    }

void number_casting(uint64_t input_rtype_hash, void *input_ref, uint64_t output_rtype_hash, void *output_ref) {
    PRE_RTCALL_HOOK();
    rtype_t *input_rtype = rt_find_rtype(input_rtype_hash);
    rtype_t *output_rtype = rt_find_rtype(output_rtype_hash);
    assertf(input_rtype, "cannot find input_rtype by hash %lu", input_rtype_hash);
    assertf(output_rtype, "cannot find output_rtype by hash %lu", output_rtype_hash);

    DEBUGF("[number_casting] input_kind=%s, input_ref=%p,input_int64(%ld), output_kind=%s, output_ref=%p",
           type_kind_str[input_rtype->kind], input_ref, fetch_int_value((addr_t) input_ref, input_rtype->size),
           type_kind_str[output_rtype->kind], output_ref);

    value_casting v = {0};
    memmove(&v, input_ref, input_rtype->size);

    switch (input_rtype->kind) {
        case TYPE_FLOAT:
        case TYPE_FLOAT64: {
            _NUMBER_CASTING(output_rtype->kind, v.f64_value, v.i64_value);
        }
        case TYPE_FLOAT32: {
            _NUMBER_CASTING(output_rtype->kind, v.f32_value, v.i64_value);
        }
            // 其他类型保持不变
        case TYPE_INT:
        case TYPE_INT64:
            _NUMBER_CASTING(output_rtype->kind, v.i64_value, v.i64_value);
        case TYPE_INT32:
            _NUMBER_CASTING(output_rtype->kind, v.i32_value, v.i64_value);
        case TYPE_INT16:
            _NUMBER_CASTING(output_rtype->kind, v.i16_value, v.i64_value);
        case TYPE_INT8:
            _NUMBER_CASTING(output_rtype->kind, v.i8_value, v.i64_value);
        case TYPE_UINT:
        case TYPE_UINT64:
            _NUMBER_CASTING(output_rtype->kind, v.u64_value, v.i64_value);
        case TYPE_UINT32:
            _NUMBER_CASTING(output_rtype->kind, v.u32_value, v.i64_value);
        case TYPE_UINT16:
            _NUMBER_CASTING(output_rtype->kind, v.u16_value, v.i64_value);
        case TYPE_UINT8:
            _NUMBER_CASTING(output_rtype->kind, v.u8_value, v.i64_value);
        default:
            assert(false && "type cannot ident");
            exit(1);
    }
}

static inline void panic_dump(coroutine_t *co, caller_t *caller, char *msg) {
    // pre_rtcall_hook 中已经记录了 ret addr
    char *dump_msg;
    if (co->main) {
        dump_msg = tlsprintf("coroutine 'main' panic: '%s' at %s:%d:%d\n", msg,
                             ((fndef_t *) caller->data)->rel_path, caller->line, caller->column);
    } else {
        dump_msg = tlsprintf("coroutine '%ld' panic: '%s' at %s:%d:%d\n", co->id, msg,
                             ((fndef_t *) caller->data)->rel_path, caller->line, caller->column);
    }
    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));
    // panic msg
    exit(EXIT_FAILURE);
}

n_ptr_t *rawptr_assert(n_rawptr_t *rawptr) {
    PRE_RTCALL_HOOK();
    if (rawptr == 0) {
        DEBUGF("[rawptr_assert] raw pointer");
        rti_throw("rawptr is null, cannot assert", true);
        return 0;
    }

    return rawptr;
}

void interface_assert(n_interface_t *mu, int64_t target_rtype_hash, void *value_ref) {
    PRE_RTCALL_HOOK();
    if (mu->rtype->hash != target_rtype_hash) {
        DEBUGF("[interface_assert] type assert error, mu->rtype->kind: %s, target_rtype_hash: %ld",
               type_kind_str[mu->rtype->kind],
               target_rtype_hash);

        rti_throw("type assert error", true);
        return;
    }

    rtype_t *rtype = rt_find_rtype(target_rtype_hash);
    uint64_t size = rtype_stack_size(rtype, POINTER_SIZE);

    if (is_stack_impl(rtype->kind)) {
        memmove(value_ref, mu->value.ptr_value, size);
    } else {
        memmove(value_ref, &mu->value, size);
    }
    DEBUGF(
            "[interface_assert] success, interface_base: %p, interface_rtype_kind: %s, heap_out_size: %lu, interface_i64_value: %ld, "
            "values_ref: %p",
            mu, type_kind_str[mu->rtype->kind], size, mu->value.i64_value, value_ref);
}


/**
 * 如果断言异常则在 processor 中附加上错误
 * @param mu
 * @param target_rtype_hash
 * @param value_ref
 */
void union_assert(n_union_t *mu, int64_t target_rtype_hash, void *value_ref) {
    PRE_RTCALL_HOOK();
    if (mu->rtype->hash != target_rtype_hash) {
        DEBUGF("[union_assert] type assert error, mu->rtype->kind: %s, target_rtype_hash: %ld",
               type_kind_str[mu->rtype->kind],
               target_rtype_hash);

        rti_throw("type assert error", true);
        return;
    }

    uint64_t size = rt_rtype_stack_size(target_rtype_hash);
    memmove(value_ref, &mu->value, size);
    DEBUGF(
            "[union_assert] success, union_base: %p, union_rtype_kind: %s, heap_out_size: %lu, union_i64_value: %ld, "
            "values_ref: %p",
            mu, type_kind_str[mu->rtype->kind], size, mu->value.i64_value, value_ref);
}

bool union_is(n_union_t *mu, int64_t target_rtype_hash) {
    PRE_RTCALL_HOOK();
    return mu->rtype->hash == target_rtype_hash;
}

bool interface_is(n_interface_t *mu, int64_t target_rtype_hash) {
    PRE_RTCALL_HOOK();
    return mu->rtype->hash == target_rtype_hash;
}

/**
 * union 参考 env 中的 upvalue 处理超过 8byte 的数据
 * @param input_rtype_hash
 * @param value
 * @return
 */
n_interface_t *interface_casting(uint64_t input_rtype_hash, void *value_ref, int64_t method_count, int64_t *methods) {
    PRE_RTCALL_HOOK();
    // - 根据 input_rtype_hash 找到对应的
    rtype_t *rtype = rt_find_rtype(input_rtype_hash);
    assert(rtype && "cannot find rtype by hash");

    ASSERT_ADDR(value_ref);

    TRACEF("[union_casting] input_kind=%s, in_heap=%d", type_kind_str[rtype->kind], rtype->in_heap);


    rtype_t interface_rtype = GC_RTYPE(TYPE_INTERFACE, 4, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);

    // any_t 在 element_rtype list 中是可以预注册的，因为其 gc_bits 不会变来变去的，都是恒定不变的！
    n_interface_t *mu = rti_gc_malloc(sizeof(n_interface_t), &interface_rtype);

    if (method_count > 0) {
        mu->method_count = method_count;
        mu->methods = (int64_t *) rti_array_new(&uint64_rtype, method_count);
        // 进行数据 copy
        memmove(mu->methods, methods, method_count * POINTER_SIZE);
    }

    DEBUGF("[interface_casting] union_base: %p, memmove value_ref(%p) -> any->value(%p), size=%lu, fetch_value_8byte=%p",
           mu, value_ref,
           &mu->value, rtype_stack_size(rtype, POINTER_SIZE), (void *) fetch_addr_value((addr_t) value_ref));

    mu->rtype = rtype;
    uint64_t out_size = rtype_stack_size(rtype, POINTER_SIZE);
    if (is_stack_impl(rtype->kind)) {
        // union 进行了数据的额外缓存，并进行值 copy，不需要担心 arr/struct 这样的大数据的丢失问题
        void *new_value = rti_gc_malloc(rtype->size, rtype);
        memmove(new_value, value_ref, out_size);
        mu->value.ptr_value = new_value;
    } else {
        // 特殊类型参数处理，为了兼容 fn method 中的 self 自动化参数, self 如果是 int/struct 等类型，会自动转换为 ptr<int>
        // 如果是 vec/string 等类型，self 的类型依旧是 vec/string 等，而不是 ptr<vec>/ptr<string> 这有点多余, 因为 vec/string
        // 本来就是在堆中分配的, 传递的是一个指针, 虽然后续可以能会进行统一处理，但是目前还是需要进行特殊处理，value 中直接存放可以作为
        // fn method 传递的参数
        memmove(&mu->value, value_ref, out_size);
    }

    DEBUGF("[interface_casting] success, union_base: %p, union_rtype: %p, union_i64_value: %ld, union_ptr_value: %p",
           mu,
           mu->rtype,
           mu->value.i64_value, mu->value.ptr_value);

    return mu;
}

/**
 * union 参考 env 中的 upvalue 处理超过 8byte 的数据
 * @param input_rtype_hash
 * @param value
 * @return
 */
n_union_t *union_casting(uint64_t input_rtype_hash, void *value_ref) {
    PRE_RTCALL_HOOK();
    // - 根据 input_rtype_hash 找到对应的
    rtype_t *rtype = rt_find_rtype(input_rtype_hash);
    assert(rtype && "cannot find rtype by hash");

    ASSERT_ADDR(value_ref);

    TRACEF("[union_casting] input_kind=%s, in_heap=%d", type_kind_str[rtype->kind], rtype->in_heap);

    type_kind gc_kind = to_gc_kind(rtype->kind);
    if (is_gc_alloc(rtype->kind)) {
        gc_kind = TYPE_GC_SCAN;
    }

    rtype_t union_rtype = GC_RTYPE(TYPE_UNION, 2, gc_kind, TYPE_GC_NOSCAN);

    // any_t 在 element_rtype list 中是可以预注册的，因为其 gc_bits 不会变来变去的，都是恒定不变的！
    n_union_t *mu = rti_gc_malloc(sizeof(n_union_t), &union_rtype);

    DEBUGF("[union_casting] union_base: %p, memmove value_ref(%p) -> any->value(%p), size=%lu, fetch_value_8byte=%p",
           mu, value_ref,
           &mu->value, rtype_stack_size(rtype, POINTER_SIZE), (void *) fetch_addr_value((addr_t) value_ref));
    mu->rtype = rtype;

    uint64_t out_size = rtype_stack_size(rtype, POINTER_SIZE);
    if (is_stack_ref_big_type_kind(rtype->kind)) {
        // union 进行了数据的额外缓存，并进行值 copy，不需要担心 arr/struct 这样的大数据的丢失问题
        void *new_value = rti_gc_malloc(rtype->size, rtype);
        memmove(new_value, value_ref, out_size);
        mu->value.ptr_value = new_value;
    } else {
        memmove(&mu->value, value_ref, out_size);
    }


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
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.bool_casting] input_rtype_hash=%lu, int_value=%lu, f64_value=%f", input_rtype_hash, int_value,
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
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.iterator_next_key] iterator base=%p,rtype_hash=%lu, cursor=%ld", iterator, rtype_hash, cursor);

    // cursor 范围测试
    assert(cursor >= -1 && cursor < INT32_MAX && "cursor out of range");

    rtype_t *iterator_rtype = rt_find_rtype(rtype_hash);

    cursor += 1;
    if (iterator_rtype->kind == TYPE_VEC || iterator_rtype->kind == TYPE_STRING) {
        n_vec_t *list = iterator;
        DEBUGF("[runtime.iterator_next_key] list=%p, kind is vec, len=%lu, cap=%lu, data_base=%p, cursor=%ld", list,
               list->length,
               list->capacity, list->data, cursor);

        if (cursor >= list->length) {
            DEBUGF("[runtime.iterator_next_key] cursor('%ld') == vec.length('%ld') end", cursor, list->length);
            return -1;
        }

        DEBUGF("[runtime.iterator_next_key] list mov cursor=%ld < list.length('%ld') to key ref=%p ~ %p", cursor,
               list->length, key_ref,
               key_ref + INT_SIZE);

        memmove(key_ref, &cursor, INT_SIZE);
        return cursor;
    }

    if (iterator_rtype->kind == TYPE_MAP) {
        n_map_t *map = iterator;
        uint64_t key_size = rt_rtype_stack_size(map->key_rtype_hash);
        DEBUGF("[runtime.iterator_next_key] kind is map, len=%lu, key_base=%p, key_index=%lu, key_size=%lu",
               map->length, map->key_data,
               map->key_rtype_hash, key_size);

        if (cursor >= map->length) {
            DEBUGF("[runtime.iterator_next_key] cursor('%ld') == map.length('%ld') end", cursor, map->length);
            return -1;
        }

        memmove(key_ref, map->key_data + key_size * cursor, key_size);
        return cursor;
    }

    assert(false && "cannot support iterator type");
    exit(0);
}

int64_t iterator_next_value(void *iterator, int64_t hash, int64_t cursor, void *value_ref) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.iterator_next_value] iterator base=%p,rtype_hash=%lu, cursor=%lu, kind=%s", iterator, hash,
           cursor);

    rtype_t *iterator_rtype = rt_find_rtype(hash);

    cursor += 1;
    if (iterator_rtype->kind == TYPE_VEC || iterator_rtype->kind == TYPE_STRING) {
        n_vec_t *list = iterator;
        assert(list->element_size && "list element size is zero");
        uint64_t element_size = list->element_size;
        DEBUGF("[runtime.iterator_next_value] kind is list, len=%lu, cap=%lu, data_base=%p, value_size=%ld, cursor=%ld",
               list->length,
               list->capacity, list->data, element_size, cursor);

        if (cursor >= list->length) {
            return -1;
        }

        memmove(value_ref, list->data + element_size * cursor, element_size);
        return cursor;
    }

    if (iterator_rtype->kind == TYPE_MAP) {
        n_map_t *map = iterator;
        uint64_t value_size = rt_rtype_stack_size(map->value_rtype_hash);
        DEBUGF("[runtime.iterator_next_value] kind is map, len=%lu, key_base=%p, key_index=%lu, key_size=%lu",
               map->length, map->key_data,
               map->key_rtype_hash, value_size);

        if (cursor >= map->length) {
            return -1;
        }

        memmove(value_ref, map->value_data + value_size * cursor, value_size);
        return cursor;
    }

    if (iterator_rtype->kind == TYPE_CHAN) {
        n_chan_t *ch = iterator;
        // auto recv msg assign to value_ref
        rt_chan_recv(ch, value_ref, false);

        return 1;
    }

    assert(false && "cannot support iterator type");
    exit(0);
}

/**
 * 存在 key 的情况下 second 直接使用 take_value, 不会对 cursor 进行递增
 * 已知 cursor 的情况
 * @param hash
 * @param cursor
 * @param value_ref
 */
void iterator_take_value(void *iterator, int64_t hash, int64_t cursor, void *value_ref) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.iterator_take_value] iterator base=%p,rtype_hash=%lu, cursor=%lu, value_ref=%p", iterator,
           hash, cursor,
           value_ref);

    assert(cursor != -1 && "cannot iterator value");
    assert(hash > 0 && "rtype hash is empty");

    rtype_t *iterator_rtype = rt_find_rtype(hash);
    if (iterator_rtype->kind == TYPE_VEC || iterator_rtype->kind == TYPE_STRING) {
        n_vec_t *list = iterator;
        DEBUGF("[runtime.iterator_take_value] kind is list, base=%p, len=%lu, cap=%lu, data_base=%p, element_size=%lu",
               iterator,
               list->length, list->capacity, list->data, list->element_size);

        assert(list->element_size > 0 && "list element size is zero");

        assert(cursor < list->length && "cursor >= list->length");

        uint64_t element_size = list->element_size;

        memmove(value_ref, list->data + element_size * cursor, element_size);
        DEBUGF("[runtime.iterator_take_value] iterator=%p, value_ref=%p, element_size=%lu", iterator, value_ref,
               element_size);
        return;
    }

    if (iterator_rtype->kind == TYPE_MAP) {
        n_map_t *map = iterator;
        uint64_t value_size = rt_rtype_stack_size(map->value_rtype_hash);
        DEBUGF("[runtime.iterator_take_value] kind is map, len=%lu, value_base=%p, value_index=%lu, value_size=%lu",
               map->length,
               map->value_data, map->value_rtype_hash, value_size);

        assert(cursor < map->length && "terator_take_value cursor >= map->length");

        memmove(value_ref, map->value_data + value_size * cursor, value_size);
        return;
    }

    assert(false && "cannot support iterator type");
    exit(0);
}

// 基于字符串到快速设置不太需要考虑内存泄漏的问题， raw_string 都是 .data 段中的字符串
void co_throw_error(n_interface_t *error, char *path, char *fn_name, n_int_t line, n_int_t column) {
    PRE_RTCALL_HOOK();

    assert(error->method_count == 1);
    coroutine_t *co = coroutine_get();

    DEBUGF("[runtime.co_throw_error] co=%p, error=%p, path=%s, line=%ld, column=%ld, msg=%s", co, (void *) error, path,
           line,
           column, (char *) rt_string_ref(rti_error_msg(error)));

    assert(co->traces == NULL);
    n_vec_t *traces = rti_vec_new(&errort_trace_rtype, 0, 0);
    rti_write_barrier_ptr(&co->traces, traces, false);

    n_trace_t trace = {
            .path = string_new(path, strlen(path)),
            .ident = string_new(fn_name, strlen(fn_name)),
            .line = line,
            .column = column,
    };
    rt_vec_push(co->traces, errort_trace_rtype.hash, &trace);

    rti_write_barrier_ptr(&co->error, error, false);
    co->has_error = true;

    post_rtcall_hook("co_throw_error");
}

void throw_index_out_error(n_int_t *index, n_int_t *len, n_bool_t be_catch) {
    PRE_RTCALL_HOOK();

    coroutine_t *co = coroutine_get();

    assert(co->scan_ret_addr);
    caller_t *caller = sc_map_get_64v(&rt_caller_map, co->scan_ret_addr);
    assert(caller);

    char *msg = tlsprintf("index out of vec [%d] with length %d", index, len);

    if (be_catch) {
        n_interface_t *error = n_error_new(string_new(msg, strlen(msg)), true);
        assert(error->method_count == 1);

        DEBUGF("[runtime.co_throw_error_msg] co=%p, error=%p, path=%s, line=%ld, column=%ld, msg=%s", co, (void *) error, path,
               line,
               column, (char *) msg);

        assert(co->traces == NULL);

        fndef_t *caller_fn = caller->data;

        n_vec_t *traces = rti_vec_new(&errort_trace_rtype, 0, 0);
        rti_write_barrier_ptr(&co->traces, traces, false);
        n_trace_t trace = {
                .path = string_new(caller_fn->rel_path, strlen(caller_fn->rel_path)),
                .ident = string_new(caller_fn->name, strlen(caller_fn->name)),
                .line = caller->line,
                .column = caller->column,
        };
        rt_vec_push(co->traces, errort_trace_rtype.hash, &trace);
        rti_write_barrier_ptr(&co->error, error, false);
        co->has_error = true;
    } else {
        char *copy_msg = strdup(msg);
        panic_dump(co, caller, copy_msg);
        free(copy_msg);
    }

    post_rtcall_hook("co_throw_error");
}

n_interface_t *co_remove_error() {
    PRE_RTCALL_HOOK();
    coroutine_t *co = coroutine_get();

    assert(co->error);
    co->has_error = false;

    n_interface_t *error = co->error;

    rti_write_barrier_ptr(&co->error, NULL, false);
    rti_write_barrier_ptr(&co->traces, NULL, false);

    post_rtcall_hook("co_remove_error");
    return error;
}

uint8_t co_has_panic(bool be_catch, char *path, char *fn_name, n_int_t line, n_int_t column) {
    coroutine_t *co = coroutine_get();
    if (!co->has_error) {
        return 0;
    }

    PRE_RTCALL_HOOK();

    assert(line >= 0 && line < 1000000);
    assert(column >= 0 && column < 1000000);
    assert(co->traces);

    // build in panic 可以被 catch 捕获，但只能是立刻捕获，否则会全局异常退出。
    if (be_catch) {
        // 存在异常时顺便添加调用栈信息, 这样 catch 错误时可以更加准确的添加相关信息
        n_trace_t trace = {
                .path = string_new(path, strlen(path)),
                .ident = string_new(fn_name, strlen(fn_name)),
                .line = line,
                .column = column,
        };

        rt_vec_push(co->traces, errort_trace_rtype.hash, &trace);
        post_rtcall_hook("co_has_panic");
        return 1;
    }

    assert(co->error);
    n_string_t *msg = rti_error_msg(co->error);

    char *dump_msg;
    if (co->main) {
        dump_msg = tlsprintf("coroutine 'main' panic: '%s' at %s:%d:%d\n", (char *) rt_string_ref(msg),
                             path, line, column);
    } else {
        dump_msg = tlsprintf("coroutine %ld panic: '%s' at %s:%d:%d\n", co->id, (char *) rt_string_ref(msg),
                             path, line, column);
    }

    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));
    // panic msg
    exit(EXIT_FAILURE);
}

uint8_t co_has_error(char *path, char *fn_name, n_int_t line, n_int_t column) {
    coroutine_t *co = coroutine_get();
    if (!co->has_error) {
        return 0;
    }

    PRE_RTCALL_HOOK();

    DEBUGF("[runtime.co_has_error] error has, fn_name: %s, line: %ld, column: %ld",
           fn_name, line, column)
    assert(line >= 0 && line < 1000000);
    assert(column >= 0 && column < 1000000);
    assert(co->traces);

    // 存在异常时顺便添加调用栈信息, 这样 catch 错误时可以更加准确的添加相关信息
    n_trace_t trace = {
            .path = string_new(path, strlen(path)),
            .ident = string_new(fn_name, strlen(fn_name)),
            .line = line,
            .column = column,
    };

    rt_vec_push(co->traces, errort_trace_rtype.hash, &trace);

    post_rtcall_hook("co_has_error");
    return 1;
}

n_anyptr_t anyptr_casting(value_casting v) {
    PRE_RTCALL_HOOK();
    return v.u64_value;
}

value_casting casting_to_anyptr(void *ptr) {
    value_casting v = {0};
    v.ptr_value = ptr;
    return v;
}

n_vec_t *std_args() {
    // 初始化一个 string 类型的数组
    n_vec_t *list = rti_vec_new(&std_arg_rtype, command_argc, command_argc);

    // 初始化 string
    for (int i = 0; i < command_argc; ++i) {
        DEBUGF("[std_args] command_argv[%d]='%s'\n", i, command_argv[i]);
        n_string_t *str = string_new(command_argv[i], strlen(command_argv[i]));
        rt_vec_assign(list, i, &str);
    }

    DEBUGF("[std_args] list=%p, list->data=%p, list->length=%lu, element_size=%lu", list, list->data,
           list->length,
           list->element_size);
    return list;
}

/**
 * ref 可能是栈上，数组中，全局变量中存储的 rtype 中的值
 * 需要感觉 rtype 存放的具体位置综合判断
 * @param rtype
 * @param ref
 * @return
 */
char *rtype_value_to_str(rtype_t *rtype, void *data_ref) {
    assert(rtype && "rtype is null");
    assert(data_ref && "data_ref is null");
    uint64_t data_size = rtype_stack_size(rtype, POINTER_SIZE);

    TRACEF("[rtype_value_str] rtype_kind=%s, data_ref=%p, data_size=%lu", type_kind_str[rtype->kind], data_ref,
           data_size);

    if (is_number(rtype->kind) || rtype->kind == TYPE_BOOL || rtype->kind == TYPE_PTR || rtype->kind == TYPE_RAWPTR ||
        rtype->kind == TYPE_ANYPTR || rtype->kind == TYPE_CHAN || rtype->kind == TYPE_COROUTINE_T) {
        assert(data_size <= 8 && "not support number size > 8");
        int64_t temp = 0;
        memmove(&temp, data_ref, data_size);
        return itoa(temp);
    }

    if (rtype->kind == TYPE_STRING) {
        n_string_t *n_str = (void *) fetch_addr_value((addr_t) data_ref);// 读取栈中存储的值

        assert(n_str && n_str->length >= 0 && "fetch addr by data ref failed");

        // return strdup(string_ref(n_str));
        // 进行 data copy, 避免被 free
        char *str = mallocz(n_str->length + 1);
        memmove(str, n_str->data, n_str->length);
        str[n_str->length] = '\0';
        return str;
    }

    if (rtype->kind == TYPE_STRUCT || rtype->kind == TYPE_ARR) {
        char *data = mallocz(data_size + 1);
        memmove(data, data_ref, data_size);
        data[data_size] = '\0';
        return data;
    }


    assert(false && "not support type kind");

    return NULL;
}

// mark_black_new_obj 如果 new_obj 不是从 allocator(gc_malloc) 获取的新对象，则有必要主动 mark black 避免其被 sweep
void rti_write_barrier_ptr(void *slot, void *new_obj, bool mark_black_new_obj) {
    DEBUGF("[runtime_gc.rt_write_barrier_ptr] slot=%p, new_obj=%p", slot, new_obj);

    n_processor_t *p = processor_get();

    // 独享线程进行 write barrier 之前需要尝试获取线程锁, 避免与 gc_work 和 barrier 冲突
    // TODO 必须放在 gc_barrier_get 之前进行独享线程的 stw locker lock? 因为 stw locker 代替了 solo p 真正的 STW?
    if (!p->share) {
        mutex_lock(&p->gc_stw_locker);
    }

    if (!gc_barrier_get()) {
        DEBUGF("[runtime_gc.rt_write_barrier_ptr] slot: %p, new_obj: %p, gc_barrier is false, no need write barrier", slot, new_obj);

        *(void **) slot = new_obj;

        if (!p->share) {
            mutex_unlock(&p->gc_stw_locker);
        }

        return;
    }

    // yuasa 删除写屏障 shade slot
    shade_obj_grey(slot);

    // stack 扫描完成后退化成黑色写屏障, 否则是是灰色写屏障
    coroutine_t *co = coroutine_get();
    bool is_grey = co->gc_black < memory->gc_count;
    TDEBUGF("[runtime_gc.rt_write_barrier_ptr] slot: %p, new_obj: %p, gc_barrier is true, gc_black %d", slot, new_obj, is_grey);

    // 直接 mark 为黑色，当前 new_obj 的 field 不会被处理，并且该 obj 本轮 gc 不会被清理
    // 例如 global linkco cache 中的获取的新的 obj, 不是从 allocator 中申请，所以需要主动进行 mark
    if (mark_black_new_obj && new_obj) {
        mark_ptr_black(new_obj);
    }

    // Dijkstra 写屏障
    // new_obj 可能为 null
    if (is_grey && new_obj) {
        // shade new_obj
        shade_obj_grey(new_obj);
    }

    *(void **) slot = new_obj;

    if (!p->share) {
        mutex_unlock(&p->gc_stw_locker);
    }
}

//static void rt_write_barrier(void *slot, void *new_obj) {
//    DEBUGF("[runtime.write_barrier] slot=%p, new_obj=%p", slot, new_obj);
//
//    n_processor_t *p = processor_get();
//
//    // 独享线程进行 write barrier 之前需要尝试获取线程锁, 避免与 gc_work 和 barrier 冲突
//    // TODO 必须放在 gc_barrier_get 之前进行独享线程的 stw locker lock? 因为 stw locker 代替了 solo p 真正的 STW?
//    if (!p->share) {
//        mutex_lock(&p->gc_stw_locker);
//    }
//
//    if (!gc_barrier_get()) {
//        RDEBUGF("[runtime.write_barrier] gc_barrier is false, no need write barrier");
//
//        memmove(slot, new_obj, POINTER_SIZE);
//
//        if (!p->share) {
//            mutex_unlock(&p->gc_stw_locker);
//        }
//
//        return;
//    }
//
//
//    RDEBUGF("[runtime.write_barrier] gc_barrier is true");
//
//    // yuasa 写屏障 shade slot
//    shade_obj_grey(slot);
//
//    // Dijkstra 写屏障
//    coroutine_t *co = coroutine_get();
//    if (co->gc_black < memory->gc_count) {
//        // shade new_obj
//        shade_obj_grey(new_obj);
//    }
//
//    memmove(slot, new_obj, POINTER_SIZE);
//
//    if (!p->share) {
//        mutex_unlock(&p->gc_stw_locker);
//    }
//}

void write_barrier(void *slot, void *new_obj) {
    PRE_RTCALL_HOOK();

    rti_write_barrier_ptr(slot, new_obj, false);
}

void rawptr_valid(void *rawptr) {
    PRE_RTCALL_HOOK();// 修改状态避免抢占

    DEBUGF("[rawptr_valid] rawptr=%p", rawptr);
    if (rawptr <= 0) {
        rti_throw("invalid memory address or nil pointer dereference", true);
    }
}


void rt_panic(n_string_t *msg) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();


    // 更新 ret addr 到 co 中
    CO_SCAN_REQUIRE(co);
    assert(co->scan_ret_addr);
    caller_t *caller = sc_map_get_64v(&rt_caller_map, co->scan_ret_addr);
    panic_dump(co, caller, rt_string_ref(msg));
}

void rt_assert(n_bool_t cond) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();

    if (cond) {
        return;
    }

    // panic
    // 更新 ret addr 到 co 中
    CO_SCAN_REQUIRE(co);
    assert(co->scan_ret_addr);
    caller_t *caller = sc_map_get_64v(&rt_caller_map, co->scan_ret_addr);
    panic_dump(co, caller, "assertion failed");
}

typedef struct {
    int64_t a;
    uint8_t b[5];
} st;

n_string_t *rt_string_new(n_anyptr_t raw_string) {
    if (!raw_string) {
        rti_throw("raw string is empty", false);
        return NULL;
    }

    char *str = (char *) raw_string;

    return string_new(str, strlen(str));
}

n_string_t *rt_strerror() {
    char *msg = strerror(errno);
    n_string_t *s = string_new(msg, strlen(msg));
    return s;
}

extern char **environ;

n_vec_t *rt_get_envs() {
    n_vec_t *list = rti_vec_new(&os_env_rtype, 0, 0);

    char **env = environ;

    while (*env) {
        n_string_t *s = string_new(*env, strlen(*env));

        rt_vec_push(list, string_rtype.hash, &s);
        env++;
    }

    DEBUGF("[libc_get_envs] list=%p, list->length=%lu", list, list->length);
    return list;
}

n_int_t rt_errno() {
    return errno;
}

n_vec_t *unsafe_vec_new(int64_t hash, int64_t element_hash, int64_t len, void *data_ptr) {
    DEBUGF("[unsafe_vec_new] hash=%lu, element_hash=%lu, len=%lu, rhash, ele_rhash, length, capacity")
    assert(len > 0);

    int64_t cap = len;

    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype && "cannot find element_rtype_hash with hash");

    // - 进行内存申请,申请回来一段内存是 memory_vec_t 大小的内存, memory_vec_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 vec_data + 1 时会按照 sizeof(memory_vec_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    n_vec_t *vec = rti_gc_malloc(vec_rtype.size, &vec_rtype);
    vec->capacity = cap;
    vec->length = len;
    vec->element_size = rtype_stack_size(element_rtype, POINTER_SIZE);
    vec->hash = hash;
    vec->data = data_ptr;

    DEBUGF("[rt_vec_new] success, vec=%p, data=%p, element_rtype_hash=%lu", vec, vec->data, vec->element_hash);
    return vec;
}
