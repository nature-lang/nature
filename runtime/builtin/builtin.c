#include "builtin.h"
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include "utils/assertf.h"
#include "runtime/type/string.h"
#include "runtime/type/any.h"

static char sprint_buf[1024];

// type_any_t 是 nature 中的类型，在 c 中是认不出来的
// 这里按理来说应该填写 void*, 写 type_any_t 就是为了语义明确
static void print_arg(any_t *arg) {
//    assert(arg->type_base > 0 && arg->type_base <= 100 && "type base unexpect");
    // TODO 这里是 type_string_t, 而不是 string_t 的实际存储的数据！, 那实际存储的数据在哪呢？
    // TODO 这里不应该是 type_string_t, type_string_t 是 nature 的类型描述数据, c 可认不得这个
    if (arg->type->kind == TYPE_STRING) {
        // TODO c 里面认识的字符串应该是内存视角的数据，而不是描述视角的数据
        // type_string_t 在内存视角，应该是由 2 块内存组成，一个是字符个数，一个是指向的数据结构(同样是内存视角)

        type_string_t *s = arg->value;
        write(STDOUT_FILENO, string_addr(s), string_length(s));
        return;
    }
    if (arg->type_base == TYPE_FLOAT) {
        int n = sprintf(sprint_buf, "%.5f", arg->float_value);
        write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->type_base == TYPE_INT) {
        int n = sprintf(sprint_buf, "%ld", arg->int_value);
        write(STDOUT_FILENO, sprint_buf, n);
        return;
    }
    if (arg->type_base == TYPE_BOOL) {
        char *raw = "false";
        if (arg->bool_value) {
            raw = "true";
        }
        write(STDOUT_FILENO, raw, strlen(raw));
        return;
    }

    assert(false && "unsupported type");
}

void print(type_array_t *args) {
    for (int i = 0; i < args->count; ++i) {
        type_t *arg = *((type_t **) args->data + i);
        print_arg(arg);
    }
}

void println(type_array_t *args) {
    print(args);
    write(STDOUT_FILENO, "\n", 1);
}