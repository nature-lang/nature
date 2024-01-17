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
    safe_free(temp);
    return result;
}

n_string_t *libc_strerror() {
    char *msg = strerror(errno);
    n_string_t *s = string_new(msg, strlen(msg));
    return s;
}

n_vec_t *libc_get_envs() {
    rtype_t *list_rtype = gc_rtype(TYPE_VEC, 4, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    rtype_t *element_rtype = gc_rtype(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    n_vec_t *list = vec_new(list_rtype->hash, element_rtype->hash, 0, 0);

    char **env = environ;

    while (*env) {
        n_string_t *s = string_new(*env, strlen(*env));
        vec_push(list, &s);
        env++;
    }

    DEBUGF("[libc_get_envs] list=%p, list->length=%lu", list, list->length);
    return list;
}

n_int_t libc_errno() {
    return errno;
}
