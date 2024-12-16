#include "libc.h"
#include "string.h"
#include "errno.h"
#include "runtime/processor.h"
#include <dirent.h>

extern char **environ;

typedef struct {
    int64_t a;
    uint8_t b[5];
} st;

n_string_t *libc_string_new(n_void_ptr_t raw_string) {
    if (!raw_string) {
        rt_coroutine_set_error("raw string is empty", false);
        return NULL;
    }

    char *str = (char *) raw_string;

    return string_new(str, strlen(str));
}

n_string_t *libc_string_replace(n_string_t *str, n_string_t *old, n_string_t *new) {
    char *temp = str_replace((char *) str->data, (char *) old->data, (char *) new->data);

    n_string_t *result = libc_string_new((n_void_ptr_t) temp);
    free(temp);
    return result;
}

n_string_t *libc_strerror() {
    char *msg = strerror(errno);
    n_string_t *s = string_new(msg, strlen(msg));
    return s;
}

n_vec_t *libc_get_envs() {
    rtype_t *element_rtype = gc_rtype(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    n_vec_t *list = rti_vec_new(element_rtype, 0, 0);

    char **env = environ;

    while (*env) {
        n_string_t *s = string_new(*env, strlen(*env));
        rt_vec_push(list, &s);
        env++;
    }

    DEBUGF("[libc_get_envs] list=%p, list->length=%lu", list, list->length);
    return list;
}

n_int_t libc_errno() {
    return errno;
}