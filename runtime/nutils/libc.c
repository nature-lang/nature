#include "libc.h"
#include "string.h"
#include "errno.h"
#include "runtime/processor.h"
#include <dirent.h>

typedef struct {
    int64_t a;
    uint8_t b[5];
} st;

n_string_t *libc_string_new(n_cptr_t raw_string) {
    if (!raw_string) {
        rt_processor_attach_errort("raw string is empty");
        return NULL;
    }

    char *str = (char *) raw_string;

    return string_new(str, strlen(str));
}

n_string_t *libc_string_replace(n_string_t *str, n_string_t *old, n_string_t *new) {
    char *temp = str_replace((char *) str->data, (char *) old->data, (char *) new->data);

    n_string_t *result = libc_string_new((n_cptr_t) temp);
    free(temp);
    return result;
}

n_string_t *libc_strerror() {
    char *msg = strerror(errno);
    n_string_t *s = string_new(msg, strlen(msg));
    return s;
}

n_string_t *libc_string_fit(n_string_t *str) {
    str->length = strlen((char *) str->data);
    return str;
}

void libc_list_fit_len(n_list_t *list, n_int_t new_len) {
    if (list->length < new_len) {
        char *msg = dsprintf("cannot set len %d to list(len: %d)", new_len, list->length);
        rt_processor_attach_errort(msg);
        return;
    }

    list->length = new_len;
}
