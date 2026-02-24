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

static inline void panic_dump(coroutine_t *co, caller_t *caller, char *msg) {
    // pre_rtcall_hook 中已经记录了 ret addr
    char *dump_msg;
    uint64_t relpath_offset = ((fndef_t *) caller->data)->relpath_offset;
    if (co->main) {
        dump_msg = tlsprintf("coroutine 'main' panic: '%s' at %s:%d:%d\n", msg,
                             STRTABLE(relpath_offset), caller->line, caller->column);
    } else {
        dump_msg = tlsprintf("coroutine '%ld' panic: '%s' at %s:%d:%d\n", co->id, msg,
                             STRTABLE(relpath_offset), caller->line, caller->column);
    }
    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));
    // panic msg
    exit(EXIT_FAILURE);
}

n_ptr_t *ptr_assert(n_ptr_t *ptr) {
    if (ptr == 0) {
        DEBUGF("[ptr_assert] raw pointer");
        rti_throw("ptr is null, cannot assert", true);
        return 0;
    }

    return ptr;
}

void interface_assert(n_interface_t *mu, int64_t target_rtype_hash, void *value_ref) {
    if (mu->rtype->hash != target_rtype_hash) {
        DEBUGF("[interface_assert] type assert failed, mu->rtype->kind: %s, target_rtype_hash: %ld",
               type_kind_str[mu->rtype->kind],
               target_rtype_hash);

        rti_throw("type assert failed", true);
        return;
    }

    rtype_t *rtype = rt_find_rtype(target_rtype_hash);
    uint64_t size = rtype->storage_size;

    if (rtype->storage_kind != STORAGE_KIND_PTR) {
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
    if (mu->rtype->hash != target_rtype_hash) {
        DEBUGF("[union_assert] type assert failed, mu->rtype->kind: %s, target_rtype_hash: %ld",
                type_kind_str[mu->rtype->kind],
                target_rtype_hash);

        rti_throw("type assert failed", true);
        return;
    }

    rtype_t *rtype = rt_find_rtype(target_rtype_hash);
    if (rtype->storage_kind == STORAGE_KIND_IND) {
        memmove(value_ref, &mu->value.struct_, rtype->storage_size);
    } else {
        memmove(value_ref, &mu->value, rtype->storage_size);
    }
    DEBUGF(
        "[union_assert] success, union_base: %p, union_rtype_kind: %s, heap_out_size: %lu, union_i64_value: %ld, "
        "values_ref: %p",
        mu, type_kind_str[mu->rtype->kind], rtype->storage_size, mu->value.i64_value, value_ref);
}

void any_assert(n_any_t *mu, int64_t target_rtype_hash, void *value_ref) {
    if (mu->rtype->hash != target_rtype_hash) {
        DEBUGF("[any_assert] type assert failed, mu->rtype->kind: %s, target_rtype_hash: %ld",
               type_kind_str[mu->rtype->kind],
               target_rtype_hash);

        rti_throw("type assert failed", true);
        return;
    }

    rtype_t *rtype = rt_find_rtype(target_rtype_hash);
    if (rtype->storage_kind == STORAGE_KIND_IND) {
        memmove(value_ref, mu->value.ptr_value, rtype->storage_size);
    } else {
        memmove(value_ref, &mu->value, rtype->storage_size);
    }
    DEBUGF(
        "[any_assert] success, any_base: %p, any_rtype_kind: %s, heap_out_size: %lu, any_i64_value: %ld, "
        "values_ref: %p",
        mu, type_kind_str[mu->rtype->kind], rtype->storage_size, mu->value.i64_value, value_ref);
}

bool union_is(n_union_t *mu, int64_t target_rtype_hash) {
    return mu->rtype->hash == target_rtype_hash;
}

bool any_is(n_any_t *mu, int64_t target_rtype_hash) {
    return mu->rtype->hash == target_rtype_hash;
}

bool interface_is(n_interface_t *mu, int64_t target_rtype_hash) {
    return mu->rtype->hash == target_rtype_hash;
}

/**
 * interface 参考 any 处理 storage_ind 类型
 * @param out
 * @param input_rtype_hash
 * @param value_ref
 * @param method_count
 * @param methods
 */
void interface_casting(n_interface_t *out, uint64_t input_rtype_hash, void *value_ref, int64_t method_count, int64_t *methods) {
    assert(out && "interface_casting out is null");
    // - 根据 input_rtype_hash 找到对应的
    rtype_t *rtype = rt_find_rtype(input_rtype_hash);
    assert(rtype && "cannot find rtype by hash");

    ASSERT_ADDR(value_ref);

    TRACEF("[interface_casting] input_kind=%s", type_kind_str[rtype->kind]);

    if (method_count > 0) {
        out->method_count = method_count;
        out->methods = (int64_t *) rti_array_new(&uint64_rtype, method_count);
        // 进行数据 copy TODO write barrier
        memmove(out->methods, methods, method_count * POINTER_SIZE);
    } else {
        out->method_count = 0;
        out->methods = NULL;
    }

    DEBUGF(
        "[interface_casting] union_base: %p, memmove value_ref(%p) -> any->value(%p), size=%lu, fetch_value_8byte=%p",
        out, value_ref,
        &out->value, rtype->storage_size, (void *) fetch_addr_value((addr_t) value_ref));

    out->rtype = rtype;
    uint64_t stack_size = rtype->storage_size;
    out->value.i64_value = 0;

    if (rtype->storage_kind != STORAGE_KIND_PTR) {
        // TODO number 可以直接存储在 value 中
        // union 进行了数据的额外缓存，并进行值 copy，不需要担心 arr/struct 这样的大数据的丢失问题
        void *new_value = rti_gc_malloc(rtype->gc_heap_size, rtype);
        memmove(new_value, value_ref, stack_size);
        out->value.ptr_value = new_value;
    } else {
        // 特殊类型参数处理，为了兼容 fn method 中的 self 自动化参数, self 如果是 int/struct 等类型，会自动转换为 ref<int>
        // 如果是 vec/string 等类型，self 的类型依旧是 vec/string 等，而不是 ref<vec>/ref<string> 这有点多余, 因为 vec/string
        // 本来就是在堆中分配的, 传递的是一个指针, 虽然后续可以能会进行统一处理，但是目前还是需要进行特殊处理，value 中直接存放可以作为
        // fn method 传递的参数
        memmove(&out->value, value_ref, stack_size);
    }

    DEBUGF("[interface_casting] success, union_base: %p, union_rtype: %p, union_i64_value: %ld, union_ptr_value: %p",
           out,
           out->rtype,
           out->value.i64_value, out->value.ptr_value);
}

/**
 * union 参考 env 中的 upvalue 处理超过 8byte 的数据
 * @param input_rtype_hash
 * @param value
 * @return
 */
void union_casting(n_union_t *out, int64_t input_rtype_hash, void *value_ref) {
    assert(out && "union_casting out is null");
    // - 根据 input_rtype_hash 找到对应的
    rtype_t *rtype = rt_find_rtype(input_rtype_hash);
    assert(rtype && "cannot find rtype by hash");

    ASSERT_ADDR(value_ref);

    DEBUGF("[union_casting] hash=%ld input_kind=%d", input_rtype_hash, rtype->kind);
    assert(rtype->kind < 1000);
    DEBUGF("[union_casting] union_base: %p, memmove value_ref(%p) -> any->value(%p), size=%lu, fetch_value_8byte=%p",
           out, value_ref,
           &out->value, rtype->storage_size, (void *) fetch_addr_value((addr_t) value_ref));
    out->rtype = rtype;

    uint64_t storage_size = rtype->storage_size;
    out->value.i64_value = 0;

    if (rtype->storage_kind == STORAGE_KIND_IND) {
        memmove(&out->value.struct_, value_ref, storage_size);
    } else {
        memmove(&out->value, value_ref, storage_size);
    }


    DEBUGF("[union_casting] success, union_base: %p, union_rtype: %p, union_i64_value: %ld", out, out->rtype,
           out->value.i64_value);
}

/**
 * any 参考旧版 union 处理 storage_ind 类型
 * @param input_rtype_hash
 * @param value_ref
 * @return
 */
void any_casting(n_any_t *out, int64_t input_rtype_hash, void *value_ref) {
    assert(out && "any_casting out is null");
    // - 根据 input_rtype_hash 找到对应的
    rtype_t *rtype = rt_find_rtype(input_rtype_hash);
    assert(rtype && "cannot find rtype by hash");

    ASSERT_ADDR(value_ref);

    TRACEF("[any_casting] input_kind=%s", type_kind_str[rtype->kind]);

    DEBUGF("[any_casting] any_base: %p, memmove value_ref(%p) -> any->value(%p), size=%lu, fetch_value_8byte=%p",
           out, value_ref,
           &out->value, rtype->storage_size, (void *) fetch_addr_value((addr_t) value_ref));
    out->rtype = rtype;

    uint64_t storage_size = rtype->storage_size;
    out->value.i64_value = 0;

    if (rtype->storage_kind == STORAGE_KIND_IND) {
        void *new_value = rti_gc_malloc(rtype->gc_heap_size, rtype);
        memmove(new_value, value_ref, storage_size);
        out->value.ptr_value = new_value;
    } else {
        memmove(&out->value, value_ref, storage_size);
    }

    DEBUGF("[any_casting] success, any_base: %p, any_rtype: %p, any_i64_value: %ld", out, out->rtype,
           out->value.i64_value);
}

/**
 * union -> any: 提取 union 的 rtype 和 value 传递给 any_casting
 */
void union_to_any(n_any_t *out, n_union_t *input) {
    assert(out && "union_to_any out is null");
    assert(input && "union_to_any input is null");
    assert(input->rtype && "union_to_any input rtype is null");

    rtype_t *rtype = input->rtype;

    DEBUGF("[union_to_any] input rtype kind=%s, hash=%ld", type_kind_str[rtype->kind], rtype->hash);

    // union value 根据 storage_kind 确定 value_ref
    void *value_ref;
    if (rtype->storage_kind == STORAGE_KIND_IND) {
        value_ref = &input->value.struct_;
    } else {
        value_ref = &input->value;
    }

    any_casting(out, rtype->hash, value_ref);
}

void tagged_union_casting(n_tagged_union_t *out, int64_t tag_hash, int64_t value_rtype_hash, void *value_ref) {
    assert(out && "tagged_union_casting out is null");
    DEBUGF("[tagged_union_casting] tag_hash=%ld, value_hash=%ld", tag_hash, value_rtype_hash);

    out->tag_hash = tag_hash;
    out->value.i64_value = 0;
    if (value_rtype_hash == 0) {
        return;
    }

    // - 根据 input_rtype_hash 找到对应的
    rtype_t *rtype = rt_find_rtype(value_rtype_hash);
    assert(rtype && "cannot find rtype by hash");

    ASSERT_ADDR(value_ref);

    DEBUGF(
        "[tagged_union_casting] union_base: %p, memmove value_ref(%p) -> any->value(%p), kind=%s, size=%lu, fetch_value_8byte=%p",
        out, value_ref,
        &out->value, type_kind_str[rtype->kind], rtype->storage_size, (void *) fetch_addr_value((addr_t) value_ref));

    uint64_t storage_size = rtype->storage_size;
    if (rtype->storage_kind == STORAGE_KIND_IND) {
        memmove(&out->value.struct_, value_ref, storage_size);
    } else {
        memmove(&out->value, value_ref, storage_size);
    }

    DEBUGF("[tagged_union_casting] success, base: %p, id: %ld, union_i64_value: %ld", out, out->tag_hash,
           out->value.i64_value);
}


/**
 * @param iterator
 * @param rtype_hash
 * @param cursor
 * @return
 */
int64_t iterator_next_key(void *iterator, uint64_t rtype_hash, int64_t cursor, void *key_ref) {
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
    assert(error->method_count == 1);
    coroutine_t *co = coroutine_get();

    n_string_t err_msg = rti_error_msg(error);
    DEBUGF("[runtime.co_throw_error] co=%p, error=%p, path=%s, fn_name=%s, line=%ld, column=%ld, msg=%s", co,
           (void *) error, path, fn_name,
           line,
           column, (char *) rt_string_ref(&err_msg));

    assert(co->traces.data == NULL);
    n_vec_t traces = rti_vec_new(&errort_trace_rtype, 0, 0);
    co->traces = traces;

    n_trace_t trace = {
        .path = string_new(path, strlen(path)),
        .ident = string_new(fn_name, strlen(fn_name)),
        .line = line,
        .column = column,
    };
    rt_vec_push(&co->traces, errort_trace_rtype.hash, &trace);

    rti_write_barrier_rtype(&co->error, error, &throwable_rtype);
    co->has_error = true;
}

void throw_index_out_error(n_int_t *index, n_int_t *len, n_bool_t be_catch) {
    coroutine_t *co = coroutine_get();
    addr_t ret_addr = CALLER_RET_ADDR(co);

    assert(ret_addr);

    caller_t *caller = sc_map_get_64v(&rt_caller_map, ret_addr);
    assert(caller);

    char *msg = tlsprintf("index out of range [%d] with length %d", index, len);

    if (be_catch) {
        n_interface_t error = n_error_new(string_new(msg, strlen(msg)), true);
        assert(error.method_count == 1);

        DEBUGF("[runtime.co_throw_error_msg] co=%p, error=%p, msg=%s", co, (void *) &error, (char *) msg);

        assert(co->traces.data == NULL);

        fndef_t *caller_fn = caller->data;

        n_vec_t traces = rti_vec_new(&errort_trace_rtype, 0, 0);
        co->traces = traces;
        n_trace_t trace = {
            .path = string_new(STRTABLE(caller_fn->relpath_offset), strlen(STRTABLE(caller_fn->relpath_offset))),
            .ident = string_new(STRTABLE(caller_fn->name_offset), strlen(STRTABLE(caller_fn->name_offset))),
            .line = caller->line,
            .column = caller->column,
        };
        rt_vec_push(&co->traces, errort_trace_rtype.hash, &trace);
        rti_write_barrier_rtype(&co->error, &error, &throwable_rtype);
        co->has_error = true;
    } else {
        char *copy_msg = strdup(msg);
        panic_dump(co, caller, copy_msg);
        free(copy_msg);
    }
}

n_interface_t co_remove_error() {
    coroutine_t *co = coroutine_get();

    assert(co->has_error);
    co->has_error = false;

    n_interface_t error = co->error;

    n_interface_t empty = {0};
    rti_write_barrier_rtype(&co->error, &empty, &throwable_rtype);
    co->traces = (n_vec_t){0};
    return error;
}

uint8_t co_has_panic(bool be_catch, char *path, char *fn_name, n_int_t line, n_int_t column) {
    coroutine_t *co = coroutine_get();
    if (!co->has_error) {
        return 0;
    }


    assert(line >= 0 && line < 1000000);
    assert(column >= 0 && column < 1000000);
    assert(co->traces.data);

    // build in panic 可以被 catch 捕获，但只能是立刻捕获，否则会全局异常退出。
    if (be_catch) {
        // 存在异常时顺便添加调用栈信息, 这样 catch 错误时可以更加准确的添加相关信息
        n_trace_t trace = {
            .path = string_new(path, strlen(path)),
            .ident = string_new(fn_name, strlen(fn_name)),
            .line = line,
            .column = column,
        };

        rt_vec_push(&co->traces, errort_trace_rtype.hash, &trace);
        return 1;
    }

    assert(co->has_error);

    // 在 runtime 调用 nature 代码， rti_error_msg 会让 gc scan_stack 异常，不过马上就要退出了，问题不大
    // 可以考虑增加 safepoint_lock, 避免进入 safepoint 状态
    n_string_t msg = rti_error_msg(&co->error);

    char *dump_msg;
    if (co->main) {
        dump_msg = tlsprintf("coroutine 'main' panic: '%s' at %s:%d:%d\n", (char *) rt_string_ref(&msg),
                             path, line, column);
    } else {
        dump_msg = tlsprintf("coroutine %ld panic: '%s' at %s:%d:%d\n", co->id, (char *) rt_string_ref(&msg),
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


    DEBUGF("[runtime.co_has_error] error has, fn_name: %s, line: %ld, column: %ld",
           fn_name, line, column)
    assert(line >= 0 && line < 1000000);
    assert(column >= 0 && column < 1000000);
    assert(co->traces.data);

    // 存在异常时顺便添加调用栈信息, 这样 catch 错误时可以更加准确的添加相关信息
    n_trace_t trace = {
        .path = string_new(path, strlen(path)),
        .ident = string_new(fn_name, strlen(fn_name)),
        .line = line,
        .column = column,
    };

    rt_vec_push(&co->traces, errort_trace_rtype.hash, &trace);

    return 1;
}

n_anyptr_t anyptr_casting(value_casting v) {
    return v.u64_value;
}

value_casting casting_to_anyptr(void *ptr) {
    value_casting v = {0};
    v.ptr_value = ptr;
    return v;
}

n_vec_t std_args() {
    // 初始化一个 string 类型的数组
    n_vec_t list = rti_vec_new(&std_arg_rtype, command_argc, command_argc);

    // 初始化 string
    for (int i = 0; i < command_argc; ++i) {
        DEBUGF("[std_args] command_argv[%d]='%s'\n", i, command_argv[i]);
        n_string_t str = string_new(command_argv[i], strlen(command_argv[i]));
        rti_vec_assign(&list, i, &str, &string_rtype);
    }

    DEBUGF("[std_args] list=%p, list->data=%p, list->length=%lu, element_size=%lu", &list, list.data,
           list.length,
           list.element_size);
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
    uint64_t data_size = rtype->storage_size;

    TRACEF("[rtype_value_str] rtype_kind=%s, data_ref=%p, data_size=%lu", type_kind_str[rtype->kind], data_ref,
           data_size);

    if (rtype->kind == TYPE_STRING) {
        n_string_t *n_str = (n_string_t *) data_ref;

        assert(n_str && n_str->length >= 0 && "fetch addr by data ref failed");

        // 进行 data copy, 避免被 free
        char *str = mallocz(n_str->length + 1);
        memmove(str, n_str->data, n_str->length);
        str[n_str->length] = '\0';
        return str;
    }

    if (rtype->storage_kind == STORAGE_KIND_IND) {
        char *data = mallocz(data_size + 1);
        memmove(data, data_ref, data_size);
        data[data_size] = '\0';
        return data;
    }

    // else dir and ptr
    assert(data_size <= 8 && "not support number size > 8");
    int64_t temp = 0;
    memmove(&temp, data_ref, data_size);
    return itoa(temp);

    assert(false && "not support type kind");

    return NULL;
}

// mark_black_new_obj 如果 new_obj 不是从 allocator(gc_malloc) 获取的新对象，则有必要主动 mark black 避免其被 sweep
void rti_write_barrier_ptr(void *slot, void *new_obj, bool mark_black_new_obj) {
    DEBUGF("[rt_write_barrier_ptr] slot=%p, new_obj=%p, barrier_ptr?=%d", slot, new_obj, gc_barrier_get());
    if (!gc_barrier_get()) {
        *(void **) slot = new_obj;
        return;
    }

    // yuasa 删除写屏障 shade slot
    shade_obj_grey(slot);

    // stack 扫描完成后退化成黑色写屏障, 否则是是灰色写屏障
    coroutine_t *co = coroutine_get();
    bool is_grey = co->gc_black < memory->gc_count;
    DEBUGF("[runtime_gc.rt_write_barrier_ptr] slot: %p, new_obj: %p, gc_barrier is true, gc_black %d", slot, new_obj,
           is_grey);

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
}

void write_barrier(void *slot, void *new_obj) {
    rti_write_barrier_ptr(slot, new_obj, false);
}

void rti_write_barrier_rtype(void *dst, void *src, rtype_t *rtype) {
    memmove(dst, src, rtype->storage_size);

    if (!gc_barrier_get() || rtype->last_ptr == 0) {
        return;
    }

    uint8_t *gc_bits = RTDATA(rtype->malloc_gc_bits_offset);
    if (rtype->malloc_gc_bits_offset == -1) {
        gc_bits = (uint8_t *) &rtype->gc_bits;
    }

    uint64_t slot_count = align_up(rtype->storage_size, POINTER_SIZE) / POINTER_SIZE;
    for (uint64_t i = 0; i < slot_count; ++i) {
        if (!bitmap_test(gc_bits, i)) {
            continue;
        }
        void **slot = (void **) ((uint8_t *) dst + (i * POINTER_SIZE));
        rti_write_barrier_ptr(slot, *slot, false);
    }
}

void ptr_valid(void *ptr) {
    // 修改状态避免抢占
    DEBUGF("[ptr_valid] ptr=%p", ptr);
    if (ptr <= 0) {
        rti_throw("invalid memory address or nil pointer dereference", true);
    }
}


void rt_panic(n_string_t msg) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();

    addr_t ret_addr = CALLER_RET_ADDR(co);
    assert(ret_addr);

    caller_t *caller = sc_map_get_64v(&rt_caller_map, ret_addr);
    panic_dump(co, caller, rt_string_ref(&msg));
}

bool rt_in_heap(n_anyptr_t addr) {
    return in_heap(addr);
}


void rt_assert(n_bool_t cond) {
    coroutine_t *co = coroutine_get();
    if (cond) {
        return;
    }

    // panic
    // 更新 ret addr 到 co 中
    addr_t ret_addr = CALLER_RET_ADDR(co);
    assert(ret_addr);

    caller_t *caller = sc_map_get_64v(&rt_caller_map, ret_addr);
    assert(caller);
    panic_dump(co, caller, "assertion failed");
}

typedef struct {
    int64_t a;
    uint8_t b[5];
} st;

n_string_t rt_string_new(n_anyptr_t raw_string) {
    if (!raw_string) {
        rti_throw("raw string is empty", false);
        return (n_string_t){0};
    }

    char *str = (char *) raw_string;
    return string_new(str, strlen(str));
}

void rt_string_concat_out(n_string_t *out, n_string_t *a, n_string_t *b) {
    assert(out);
    *out = string_concat(a, b);
}

void rt_string_new_with_pool_out(n_string_t *out, void *raw_string, int64_t length) {
    DEBUGF("[rt_string_new_with_pool_out] out %p, raw_string %s, len %ld", out, (char *) raw_string, length);
    assert(out);
    *out = string_new_with_pool(raw_string, length);
}

void rt_string_to_vec_out(n_vec_t *out, n_string_t *src) {
    assert(out);
    *out = string_to_vec(src);
}

void rt_vec_to_string_out(n_string_t *out, n_vec_t *src) {
    assert(out);
    *out = vec_to_string(src);
}

void rt_vec_slice_out(n_vec_t *out, n_vec_t *vec, int64_t start, int64_t end) {
    assert(out);
    *out = rt_vec_slice(vec, start, end);
}

n_string_t rt_strerror() {
    char *msg = strerror(errno);
    return string_new(msg, strlen(msg));
}

extern char **environ;

n_vec_t rt_get_envs() {
    n_vec_t list = rti_vec_new(&os_env_rtype, 0, 0);

    char **env = environ;

    while (*env) {
        n_string_t s = string_new(*env, strlen(*env));

        rt_vec_push(&list, string_rtype.hash, &s);
        env++;
    }

    DEBUGF("[libc_get_envs] list=%p, list->length=%lu", &list, list.length);
    return list;
}

n_int_t rt_errno() {
    return errno;
}

n_anyptr_t rt_array_new(int64_t element_hash, int64_t length) {
    if (length < 0) {
        rti_throw("array_new length must be non-negative", true);
        return 0;
    }

    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype && "cannot find element_rtype_hash with hash");
    return (n_anyptr_t) rti_array_new(element_rtype, (uint64_t) length);
}

n_vec_t unsafe_vec_new(int64_t hash, int64_t element_hash, int64_t len, void *data_ptr) {
    DEBUGF("[unsafe_vec_new] hash=%lu, element_hash=%lu, len=%lu, rhash, ele_rhash, length, capacity")
    assert(len > 0);

    int64_t cap = len;

    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype && "cannot find element_rtype_hash with hash");

    // - 进行内存申请,申请回来一段内存是 memory_vec_t 大小的内存, memory_vec_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 vec_data + 1 时会按照 sizeof(memory_vec_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    n_vec_t vec = {0};
    vec.capacity = cap;
    vec.length = len;
    vec.element_size = element_rtype->storage_size;
    vec.hash = hash;
    vec.data = data_ptr;

    DEBUGF("[rt_vec_new] success, vec=%p, data=%p, element_size=%lu", &vec, vec.data, vec.element_size);
    return vec;
}


n_string_t rt_string_ref_new(void *raw_string, int64_t length) {
    if (length < 0) {
        rti_throw("string_ref_new length must be non-negative", false);
        return (n_string_t){0};
    }
    n_string_t str = string_new(raw_string, length);
    DEBUGF("[rt_string_ref_new] create, str: %p", raw_string);
    return str;
}

/**
 * c 语言字符串中添加 '\0' 作为结束字符
 * @param n_str
 * @return
 */
void *rt_string_ref(n_string_t *n_str) {
    DEBUGF("[rt_string_ref] length=%lu, data=%p", n_str->length, n_str->data);
    return n_str->data;
}
