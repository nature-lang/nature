#ifndef NATURE_SET_H
#define NATURE_SET_H

#include "utils/type.h"
#include "utils/custom_links.h"
#include "runtime/memory.h"
#include "array.h"
#include "hash.h"

#define SET_DEFAULT_CAPACITY 100

memory_set_t *set_new(uint64_t rtype_index, uint64_t key_index);

bool set_add(memory_set_t *m, void *key_ref);

bool set_contains(memory_set_t *m, void *key_ref);

void set_delete(memory_set_t *m, void *key_ref);

#endif //NATURE_SET_H
