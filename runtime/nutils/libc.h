#ifndef NATURE_LIBC_H
#define NATURE_LIBC_H

#include "utils/type.h"
#include "time.h"

n_string_t *libc_string_new(n_cptr_t raw_string);

n_string_t *libc_string_replace(n_string_t *str, n_string_t *old, n_string_t *new);

n_string_t *libc_string_fit(n_string_t *str);

n_string_t *libc_strerror();

void libc_list_fit_len(n_list_t *list, n_int_t new_len);

#endif //NATURE_LIBC_H
