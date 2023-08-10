#ifndef NATURE_LIBC_H
#define NATURE_LIBC_H

#include "utils/type.h"
#include "time.h"

n_u16_t libc_htons(n_u16_t host);

n_struct_t *libc_localtime(n_int64_t timestamp);

n_int64_t libc_mktime(n_struct_t *timeinfo);

n_string_t *libc_strftime(n_struct_t *timeinfo, char *format);

#endif //NATURE_LIBC_H
