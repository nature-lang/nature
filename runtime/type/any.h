#ifndef NATURE_ANY_H
#define NATURE_ANY_H

#include "src/type.h"

/**
 * any 能够表达任意类型
 */
typedef struct {
    type_base_t type_base; // TODO 如何能够给定 type_t 则表达性会更好
    union {
        void *value;
        double float_value;
        bool bool_value;
        int int_value;
    };
} any_t;

any_t *any_new(type_base_t base, void *value) {
    any_t *a = malloc(sizeof(any_t));
    a->type_base = base;
    a->value = value;
    return a;
}

#endif //NATURE_ANY_H
