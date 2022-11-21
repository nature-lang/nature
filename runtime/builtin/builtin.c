#include "builtin.h"
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include "utils/assertf.h"

static char sprint_buf[1024];

static void print_arg(any_t *arg) {
//    assert(arg->type_base > 0 && arg->type_base <= 100 && "type base unexpect");

    if (arg->type_base == TYPE_STRING) {
        string_t *s = arg->value;
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

void print(array_t *args) {
    for (int i = 0; i < args->count; ++i) {
        any_t *arg = *((any_t **) args->data + i);
        print_arg(arg);
    }
}

void println(array_t *args) {
    print(args);
    write(STDOUT_FILENO, "\n", 1);
}