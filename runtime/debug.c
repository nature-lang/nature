#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

void debug_printf(const char *__restrict format, ...) {
//    char sprint_buf[1024];
//    va_list args;
//    va_start(args, format);
//    int n = vsprintf(sprint_buf, format, args);
//    va_end(args);
//    write(STDOUT_FILENO, sprint_buf, n);
    write(STDOUT_FILENO, format, 20);
}
