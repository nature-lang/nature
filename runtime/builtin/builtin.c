#include "builtin.h"
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

static char sprint_buf[1024];

// type_any_t 是 nature 中的类型，在 c 中是认不出来的
// 这里按理来说应该填写 void*, 写 type_any_t 就是为了语义明确
static void print_arg(memory_any_t *arg) {
    if (arg->rtype->kind == TYPE_STRING) {
        // memory_string_t 在内存视角，应该是由 2 块内存组成，一个是字符个数，一个是指向的数据结构(同样是内存视角)
        memory_string_t *s = arg->value; // 字符串的内存视角
        write(STDOUT_FILENO, s->array_data, s->length);
        return;
    }
    if (is_float(arg->rtype->kind)) {
        int n = sprintf(sprint_buf, "%.5f", arg->float_value);
        write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (is_integer(arg->rtype->kind)) {
        int n = sprintf(sprint_buf, "%ld", arg->int_value);
        write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->rtype->kind == TYPE_BOOL) {
        char *raw = "false";
        if (arg->bool_value) {
            raw = "true";
        }
        write(STDOUT_FILENO, raw, strlen(raw));
        return;
    }

    assert(false && "unsupported type");
}

void print(memory_list_t *args) {
    // any_trans 将 int 转换成了堆中的一段数据，并将堆里面的其实地址返回了回去
    // 所以 args->data 是一个堆里面的地址，其指向的堆内存区域是 [any_start_ptr1, any_start_ptr2m, ...]
    addr_t *p = (addr_t *) args->array_data; // 把 data 中存储的值赋值给 p
    for (int i = 0; i < args->length; ++i) {
        // 将 p 中存储的地址赋值给 a, 此时 a 中存储的是一个堆中的地址，其结构是 memory_any_t
        memory_any_t *any = (memory_any_t *) p[i];

        print_arg(any);
    }
}

void println(memory_list_t *args) {
    print(args);
    write(STDOUT_FILENO, "\n", 1);
}

void memory_move(void *dst, void *src, uint64_t size) {
    memmove(dst, src, size);
}
