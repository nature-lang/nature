#ifndef NATURE_STRING_H
#define NATURE_STRING_H

#include "utils/type.h"

n_string_t *string_new(void *raw_string, uint64_t length);

char *string_to_c_string_ref(n_string_t *n_str);

char *string_to_c_string(n_string_t *n_str);

#endif //NATURE_STRING_H
