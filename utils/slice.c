#include "slice.h"
#include "stdlib.h"

slice_t *slice_new() {
    slice_t *s = malloc(sizeof(slice_t));
    s->count = 0;
    s->take = malloc(sizeof(void *) * 8);
    s->capacity = 8;
    return s;
}

void slice_push(slice_t *s, void *value) {
    if (s->capacity <= s->count) {
        s->capacity = s->capacity * 2;
        s->take = realloc(s->take, sizeof(void *) * s->capacity);
    }

    s->take[s->count++] = value;
}
