#ifndef NATURE_STRING_VIEW_H
#define NATURE_STRING_VIEW_H

#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include "utils/helper.h"

typedef struct {
    char *data;
    size_t size;
} string_view_t;

// 创建 string_view_t
static inline string_view_t *string_view_create(char *str, size_t len) {
    string_view_t *sv = NEW(string_view_t);

    sv->data = mallocz(len);
    strncpy(sv->data, str, len);
    sv->size = len;
    return sv;
}

static inline bool string_view_empty(string_view_t *sv) {
    return sv->size == 0;
}

#endif //NATURE_STRING_VIEW_H
