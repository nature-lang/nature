#include "any.h"

any_t *any_new(type_base_t base, void *value) {
    any_t *a = malloc(sizeof(any_t));
    a->type_base = base;
    a->value = value;
    return a;
}