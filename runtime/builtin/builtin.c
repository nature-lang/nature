#include "builtin.h"
#include <stdio.h>
#include <unistd.h>

void builtin_print(string_t *string) {
    printf("hello world!");
}

