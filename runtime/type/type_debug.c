#include "type_debug.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void *gen_hello_world() {
    char *str = "hello world!\n";
    size_t len = strlen(str);
    void *point = malloc(len);
    strcpy(point, str);
    return point;
}