#include "libc.h"
#include "string.h"

n_string_t *libc_string_new(n_cptr_t raw_string) {
    char *str = (char *) raw_string;

    return string_new(str, strlen(str));
}

n_string_t *libc_string_replace(n_string_t *str, n_string_t *old, n_string_t *new) {
    char *temp = str_replace((char *) str->data, (char *) old->data, (char *) new->data);

    n_string_t *result = libc_string_new((n_cptr_t) temp);
    free(temp);
    return result;
}
