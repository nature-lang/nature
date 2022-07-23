#include "builtin.h"
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>

static char sprint_buf[1024];

void builtin_print(int arg_count, ...) {
    builtin_operand_t *operands[arg_count];
    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        operands[i] = va_arg(args, builtin_operand_t*);
    }
    va_end(args);

    for (int i = 0; i < arg_count; ++i) {
        builtin_operand_t *operand = operands[i];
        if (operand->type == TYPE_STRING) {
            string_t *s = operand->point_value;
            write(STDOUT_FILENO, string_addr(s), string_length(s));
            goto BUILTIN_PRINT_EXIT;
        }
        if (operand->type == TYPE_FLOAT) {
            int n = sprintf(sprint_buf, "%f", operand->float_value);
            write(STDOUT_FILENO, sprint_buf, n);
            goto BUILTIN_PRINT_EXIT;
        }
        if (operand->type == TYPE_INT) {
            int n = sprintf(sprint_buf, "%ld", operand->int_value);
            write(STDOUT_FILENO, sprint_buf, n);
            goto BUILTIN_PRINT_EXIT;
        }
        if (operand->type == TYPE_BOOL) {
            char *raw = "false";
            if (operand->bool_value) {
                raw = "true";
            }
            write(STDOUT_FILENO, raw, strlen(raw));
            goto BUILTIN_PRINT_EXIT;
        }
        write(STDOUT_FILENO, "operand type error\n", 20);
    }
    BUILTIN_PRINT_EXIT:
    return;
//    write(STDOUT_FILENO, "\n", 1);
}

builtin_operand_t *builtin_new_operand(type_category type, uint8_t *value) {
    builtin_operand_t *operand = malloc(sizeof(builtin_operand_t));
    operand->type = type;
    operand->point_value = value;
    if (type == TYPE_INT || type == TYPE_FLOAT || type == TYPE_BOOL) {
        memcpy(&operand->float_value, value, 8);
    }
    return operand;
}
