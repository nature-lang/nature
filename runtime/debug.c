#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

static char sprint_buf[1024];

void *gen_hello_world() {
    char *str = "hello world!\n";
    size_t len = strlen(str);
    void *point = malloc(len);
    strcpy(point, str);
    return point;
}

void debug_printf(const char *__restrict format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsprintf(sprint_buf, format, args);
    va_end(args);
    write(STDOUT_FILENO, sprint_buf, n);
}
