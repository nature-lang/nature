#include "builtin.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "runtime.h"
#include "runtime/memory.h"

static char *space = " ";

// type_any_t 是 nature 中的类型，在 c 中是认不出来的
// 这里按理来说应该填写 void*, 写 type_any_t 就是为了语义明确
static void print_arg(n_union_t *arg) {
    char sprint_buf[1024];

    assert(arg && "[runtime.print_arg] arg is null");
    DEBUGF("[runtime.print_arg] arg=%p, ptr_value=%p", arg, arg->value.ptr_value);
    assert(arg->rtype && "[runtime.print_arg] arg->rtype is null");
    DEBUGF("[runtime.print_arg] arg->rtype=%p, kind=%s, arg->value=%lu", arg->rtype, type_kind_str[arg->rtype->kind],
           arg->value.i64_value);

    if (arg->rtype->kind == TYPE_STRING) {
        // n_string_t 在内存视角，应该是由 2 块内存组成，一个是字符个数，一个是指向的数据结构(同样是内存视角)
        n_string_t *s = arg->value.ptr_value;// 字符串的内存视角
        assert(s && "[runtime.print_arg] string is null");

        DEBUGF("[runtime.print_arg] string=%p, length=%lu, data=%p, len=%lu, cap=%lu, hash=%ld, element_size=%ld",
               s, s->length, s->data, s->length, s->capacity, s->hash, s->element_size);


        if (s->length == 0) {// 什么也不需要输出
            DEBUGF("[runtime.print_arg] string length is 0 not write, ");
            return;
        }

        VOID write(STDOUT_FILENO, s->data, s->length);
        return;
    }

    if (arg->rtype->kind == TYPE_FLOAT64 || arg->rtype->kind == TYPE_FLOAT) {
        int n = sprintf(sprint_buf, "%f", arg->value.f64_value);
        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }

    if (arg->rtype->kind == TYPE_FLOAT32) {
        int n = sprintf(sprint_buf, "%f", arg->value.f32_value);
        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }


    if (arg->rtype->kind == TYPE_UINT || arg->rtype->kind == TYPE_UINT64) {
#ifdef __LINUX
        int n = sprintf(sprint_buf, "%lu", arg->value.u64_value);
#else
        int n = sprintf(sprint_buf, "%llu", arg->value.u64_value);
#endif
        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->rtype->kind == TYPE_UINT32) {
        int n = sprintf(sprint_buf, "%u", arg->value.u32_value);
        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->rtype->kind == TYPE_UINT16) {
        int n = sprintf(sprint_buf, "%u", arg->value.u16_value);
        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->rtype->kind == TYPE_UINT8) {
        int n = sprintf(sprint_buf, "%u", arg->value.u8_value);
        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->rtype->kind == TYPE_INT || arg->rtype->kind == TYPE_INT64) {
#ifdef __LINUX
        int n = sprintf(sprint_buf, "%ld", arg->value.i64_value);
#else
        int n = sprintf(sprint_buf, "%lld", arg->value.i64_value);
#endif

        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->rtype->kind == TYPE_INT32) {
        int n = sprintf(sprint_buf, "%d", arg->value.i32_value);
        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->rtype->kind == TYPE_INT16) {
        int n = sprintf(sprint_buf, "%d", arg->value.i16_value);
        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->rtype->kind == TYPE_INT8) {
        int n = sprintf(sprint_buf, "%d", arg->value.i8_value);
        VOID write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->rtype->kind == TYPE_BOOL) {
        char *raw = "false";
        if (arg->value.bool_value) {
            raw = "true";
        }
        VOID write(STDOUT_FILENO, raw, strlen(raw));
        return;
    }
    if (arg->rtype->kind == TYPE_NULL) {
        char *raw = "null";
        VOID write(STDOUT_FILENO, raw, strlen(raw));
        return;
    }

    // 其他所有类型都打印 ptr 地址
    int n = sprintf(sprint_buf, "%p", arg->value.ptr_value);
    VOID write(STDOUT_FILENO, sprint_buf, n);
}


void print(n_vec_t *args) {
    // any_trans 将 int 转换成了堆中的一段数据，并将堆里面的其实地址返回了回去
    // 所以 args->data 是一个堆里面的地址，其指向的堆内存区域是 [any_start_ptr1, any_start_ptr2m, ...]
    addr_t base = (addr_t) args->data;// 把 data 中存储的值赋值给 p
    uint64_t element_size = args->element_size;

    DEBUGF("[runtime.print] args vec=%p, len=%lu, data=%p, element_size=%lu", args, args->length, (void *) base,
           element_size);

    for (int i = 0; i < args->length; ++i) {
        addr_t p = base + (i * element_size);

        // 将 p 中存储的地址赋值给 a, 此时 a 中存储的是一个堆中的地址，其结构是 memory_any_t
        // void* p 只是转换为指针类型，而不会读取其中的值，所以这里 n_union_t* 是目标值, *value 才是读取 p 中的地址
        n_union_t *union_arg = (n_union_t *) fetch_addr_value(p);

        DEBUGF("[runtime.print] union arg i=%d, addr=%p, union_arg_value=%p ", i, (void *) p, union_arg);

        print_arg(union_arg);
    }
}

void println(n_vec_t *args) {
    // any_trans 将 int 转换成了堆中的一段数据，并将堆里面的其实地址返回了回去
    // 所以 args->data 是一个堆里面的地址，其指向的堆内存区域是 [any_start_ptr1, any_start_ptr2m, ...]
    addr_t base = (addr_t) args->data;// 把 data 中存储的值赋值给 p
    uint64_t element_size = args->element_size;

    DEBUGF("[runtime.println] args vec=%p, len=%lu, data=%p, element_size=%lu", args, args->length, (void *) base,
           element_size);

    for (int i = 0; i < args->length; ++i) {
        addr_t p = base + (i * element_size);

        // 将 p 中存储的地址赋值给 a, 此时 a 中存储的是一个堆中的地址，其结构是 memory_any_t
        // void* p 只是转换为指针类型，而不会读取其中的值，所以这里 n_union_t* 是目标值, *value 才是读取 p 中的地址
        n_union_t *union_arg = (n_union_t *) fetch_addr_value(p);

        DEBUGF("[runtime.println] union arg i=%d, addr=%p, union_arg_value=%p ", i, (void *) p, union_arg);

        print_arg(union_arg);

        if (i < (args->length - 1)) {
            VOID write(STDOUT_FILENO, space, 1);
        }
    }

    VOID write(STDOUT_FILENO, "\n", 1);
}
