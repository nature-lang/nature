#include "libc.h"
#include "string.h"

n_string_t *libc_string_new(n_cptr_t raw_string) {
    char *str = (char *) raw_string;

    return string_new(str, strlen(str));
}
