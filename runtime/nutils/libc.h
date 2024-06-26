#ifndef NATURE_LIBC_H
#define NATURE_LIBC_H

#include "utils/type.h"
#include "time.h"

n_string_t *libc_string_new(n_void_ptr_t raw_string);

n_string_t *libc_string_replace(n_string_t *str, n_string_t *old, n_string_t *new);

n_string_t *libc_strerror();

#endif //NATURE_LIBC_H
