#include "nutils.h"

#include "runtime/memory.h"
#include "runtime/processor.h"
#include "string.h"
#include "vec.h"
#include "runtime/rtype.h"

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
                *(uint64_t *) output_ref = (uint64_t)(int64_t) _input_value;                                                                     \
                return;                                                                                                                 \
            case TYPE_UINT32:                                                                                                           \
                *(uint32_t *) output_ref = (uint32_t)(int32_t) _input_value;                                                                     \
                return;                                                                                                                 \
            case TYPE_UINT16:                                                                                                           \
                *(uint16_t *) output_ref = (uint16_t)(int16_t) _input_value;                                                                     \
                return;                                                                                                                 \
            case TYPE_UINT8:                                                                                                            \
                *(uint8_t *) output_ref = (uint8_t)(int8_t) _input_value;                                                                       \
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
           type_kind_str[input_rtype->kind], input_ref, fetch_int_value((addr_t)input_ref, input_rtype->size),
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
            assert(false && "type cannot ident");
            exit(1);
    }
}

n_ptr_t *raw_ptr_assert(n_raw_ptr_t *raw_ptr) {
    PRE_RTCALL_HOOK();
    if (raw_ptr == 0) {
        DEBUGF("[raw_ptr_assert] raw pointer");
        rt_default_co_error("raw_ptr is null, cannot assert", true);
        return 0;
    }

    return raw_ptr;
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

        rt_default_co_error("type assert error", true);
        return;
    }

    uint64_t size = rt_rtype_out_size(target_rtype_hash);
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
    if (rtype->size > 8) {
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
    if (out_size <= 8) {
        memmove(&mu->value, value_ref, out_size);
    } else {
        void *new_value = rti_gc_malloc(rtype->size, rtype);
        memmove(new_value, value_ref, out_size);
        mu->value.ptr_value = new_value;
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
        uint64_t key_size = rt_rtype_out_size(map->key_rtype_hash);
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

int64_t iterator_next_value(void *iterator, uint64_t rtype_hash, int64_t cursor, void *value_ref) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.iterator_next_value] iterator base=%p,rtype_hash=%lu, cursor=%lu", iterator, rtype_hash, cursor);

    rtype_t *iterator_rtype = rt_find_rtype(rtype_hash);

    cursor += 1;
    if (iterator_rtype->kind == TYPE_VEC || iterator_rtype->kind == TYPE_STRING) {
        n_vec_t *list = iterator;
        assert(list->ele_rhash && "list element rtype hash is empty");
        uint64_t value_size = rt_rtype_out_size(list->ele_rhash);
        DEBUGF("[runtime.iterator_next_value] kind is list, len=%lu, cap=%lu, data_base=%p, value_size=%ld, cursor=%ld",
               list->length,
               list->capacity, list->data, value_size, cursor);

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
               map->length, map->key_data,
               map->key_rtype_hash, value_size);

        if (cursor >= map->length) {
            return -1;
        }

        memmove(value_ref, map->value_data + value_size * cursor, value_size);
        return cursor;
    }

    assert(false && "cannot support iterator type");
    exit(0);
}

void iterator_take_value(void *iterator, uint64_t rtype_hash, int64_t cursor, void *value_ref) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.iterator_take_value] iterator base=%p,rtype_hash=%lu, cursor=%lu, value_ref=%p", iterator,
           rtype_hash, cursor,
           value_ref);

    assert(cursor != -1 && "cannot iterator value");
    assert(rtype_hash > 0 && "rtype hash is empty");

    rtype_t *iterator_rtype = rt_find_rtype(rtype_hash);
    if (iterator_rtype->kind == TYPE_VEC || iterator_rtype->kind == TYPE_STRING) {
        n_vec_t *list = iterator;
        DEBUGF("[runtime.iterator_take_value] kind is list, base=%p, len=%lu, cap=%lu, data_base=%p, element_hash=%lu",
               iterator,
               list->length, list->capacity, list->data, list->ele_rhash);

        assert(list->ele_rhash > 0 && "list element rtype hash is empty");

        assert(cursor < list->length && "cursor >= list->length");

        uint64_t element_size = rt_rtype_out_size(list->ele_rhash);

        memmove(value_ref, list->data + element_size * cursor, element_size);
        DEBUGF("[runtime.iterator_take_value] iterator=%p, value_ref=%p, element_size=%lu", iterator, value_ref,
               element_size);
        return;
    }

    if (iterator_rtype->kind == TYPE_MAP) {
        n_map_t *map = iterator;
        uint64_t value_size = rt_rtype_out_size(map->value_rtype_hash);
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
void co_throw_error(n_string_t *msg, char *path, char *fn_name, n_int_t line, n_int_t column) {
    PRE_RTCALL_HOOK();

    coroutine_t *co = coroutine_get();
    DEBUGF("[runtime.co_throw_error] co=%p, msg=%s, path=%s, line=%ld, column=%ld", co, msg->data, path, line, column);

    n_error_t *error = n_error_new(msg, false);

    n_trace_t trace = {
        .path = string_new(path, strlen(path)),
        .ident = string_new(fn_name, strlen(fn_name)),
        .line = line,
        .column = column,
    };
    rt_vec_push(error->traces, &trace);

    co->error = error;

    post_rtcall_hook("co_throw_error");
}

n_error_t co_remove_error() {
    PRE_RTCALL_HOOK();
    coroutine_t *co = coroutine_get();
    assert(co->error);

    n_error_t *error = co->error;
    DEBUGF("[runtime.co_remove_error] remove error: %p, has? %d", error, error ? error->has : 0);

    co->error = NULL;

    post_rtcall_hook("co_remove_error");

    return *error;
}

uint8_t co_has_panic(bool be_catch, char *path, char *fn_name, n_int_t line, n_int_t column) {
    coroutine_t *co = coroutine_get();
    if (!co->error || co->error->has == false) {
        return 0;
    }

    PRE_RTCALL_HOOK();

    assert(line >= 0 && line < 1000000);
    assert(column >= 0 && column < 1000000);

    // build in panic 可以被 catch 捕获，但只能是立刻捕获，否则会全局异常退出。
    if (be_catch) {
        // 存在异常时顺便添加调用栈信息, 这样 catch 错误时可以更加准确的添加相关信息
        n_trace_t trace = {
            .path = string_new(path, strlen((char *) path)),
            .ident = string_new(fn_name, strlen((char *) fn_name)),
            .line = line,
            .column = column,
        };

        rt_vec_push(co->error->traces, &trace);

        post_rtcall_hook("co_has_panic");
        return 1;
    }

    char *dump_msg;
    if (co->main) {
        dump_msg = dsprintf("coroutine 'main' panic: '%s' at %s:%d:%d\n", (char *) co->error->msg->data,
                            path, line, column);
    } else {
        dump_msg = dsprintf("coroutine %ld panic: '%s' at %s:%d:%d\n", co->id, (char *) co->error->msg->data,
                            path, line, column);
    }

    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));
    // panic msg
    exit(EXIT_FAILURE);
}

uint8_t co_has_error(char *path, char *fn_name, n_int_t line, n_int_t column) {
    coroutine_t *co = coroutine_get();
    if (!co->error || co->error->has == false) {
        return 0;
    }

    PRE_RTCALL_HOOK();

    DEBUGF("[runtime.co_has_error] errort? %d, fn_name: %s, line: %ld, column: %ld", co->error ? co->error->has : 0,
           fn_name, line, column)
    assert(line >= 0 && line < 1000000);
    assert(column >= 0 && column < 1000000);

    // 存在异常时顺便添加调用栈信息, 这样 catch 错误时可以更加准确的添加相关信息
    n_trace_t trace = {
        .path = string_new(path, strlen(path)),
        .ident = string_new(fn_name, strlen(fn_name)),
        .line = line,
        .column = column,
    };

    rt_vec_push(co->error->traces, &trace);

    post_rtcall_hook("co_has_error");
    return 1;
}

n_void_ptr_t void_ptr_casting(value_casting v) {
    PRE_RTCALL_HOOK();
    return v.u64_value;
}

value_casting casting_to_void_ptr(void *ptr) {
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

    DEBUGF("[std_args] list=%p, list->data=%p, list->length=%lu, element_rtype_hash=%lu", list, list->data,
           list->length,
           list->ele_rhash);
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

    if (is_number(rtype->kind)) {
        assert(data_size <= 8 && "not support number size > 8");
        int64_t temp = 0;
        memmove(&temp, data_ref, data_size);
        return itoa(temp);
    }

    if (rtype->kind == TYPE_STRING) {
        n_string_t *n_str = (void *) fetch_addr_value((addr_t) data_ref); // 读取栈中存储的值

        assert(n_str && n_str->length >= 0 && "fetch addr by data ref failed");

        // return strdup(string_ref(n_str));
        // 进行 data copy, 避免被 free
        char *str = mallocz(n_str->length + 1);
        memmove(str, n_str->data, n_str->length);
        str[n_str->length] = '\0';
        return str;
    }

    assert(false && "not support type kind");

    return NULL;
}

void rt_write_barrier(void *slot, void *new_obj) {
    DEBUGF("[runtime.write_barrier] slot=%p, new_obj=%p", slot, new_obj);

    n_processor_t *p = processor_get();

    // 独享线程进行 write barrier 之前需要尝试获取线程锁, 避免与 gc_work 和 barrier 冲突
    // TODO 必须放在 gc_barrier_get 之前进行独享线程的 stw locker lock? 因为 stw locker 代替了 solo p 真正的 STW?
    if (!p->share) {
        mutex_lock(&p->gc_stw_locker);
    }

    if (!gc_barrier_get()) {
        RDEBUGF("[runtime.write_barrier] gc_barrier is false, no need write barrier");
        memmove(slot, new_obj, POINTER_SIZE);

        if (!p->share) {
            mutex_unlock(&p->gc_stw_locker);
        }

        return;
    }


    RDEBUGF("[runtime.write_barrier] gc_barrier is true");

    // yuasa 写屏障 shade slot
    shade_obj_grey(slot);

    // Dijkstra 写屏障
    coroutine_t *co = coroutine_get();
    if (co->gc_black < memory->gc_count) {
        // shade new_obj
        shade_obj_grey(new_obj);
    }

    memmove(slot, new_obj, POINTER_SIZE);

    if (!p->share) {
        mutex_unlock(&p->gc_stw_locker);
    }
}

void write_barrier(void *slot, void *new_obj) {
    PRE_RTCALL_HOOK();

    rt_write_barrier(slot, new_obj);
}

void raw_ptr_valid(void *raw_ptr) {
    PRE_RTCALL_HOOK(); // 修改状态避免抢占

    DEBUGF("[raw_ptr_valid] raw_ptr=%p", raw_ptr);
    if (raw_ptr <= 0) {
        rt_default_co_error("invalid memory address or nil pointer dereference", true);
    }
}

static inline void panic_dump(coroutine_t *co, caller_t *caller, char *msg) {
    // pre_rtcall_hook 中已经记录了 ret addr
    char *dump_msg;
    if (co->main) {
        dump_msg = dsprintf("coroutine 'main' panic: '%s' at %s:%d:%d\n", msg,
                            ((fndef_t *) caller->data)->rel_path, caller->line, caller->column);
    } else {
        dump_msg = dsprintf("coroutine '%ld' panic: '%s' at %s:%d:%d\n", co->id, msg,
                            ((fndef_t *) caller->data)->rel_path, caller->line, caller->column);
    }
    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));
    // panic msg
    exit(EXIT_FAILURE);
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

n_string_t *rt_string_new(n_void_ptr_t raw_string) {
    if (!raw_string) {
        rt_default_co_error("raw string is empty", false);
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
        rt_vec_push(list, &s);
        env++;
    }

    DEBUGF("[libc_get_envs] list=%p, list->length=%lu", list, list->length);
    return list;
}

n_int_t rt_errno() {
    return errno;
}
