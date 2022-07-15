#include "builtin.h"
#include <unistd.h>

void builtin_print(string_t *s) {
    int len = string_length(s);
    void *str = string_addr(s);
    write(STDOUT_FILENO, str, len);
}

