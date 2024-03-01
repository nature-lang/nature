#ifndef NATURE_SET_H
#define NATURE_SET_H

#include "array.h"
#include "hash.h"
#include "runtime/memory.h"
#include "utils/custom_links.h"
#include "utils/type.h"

#define SET_DEFAULT_CAPACITY 100

n_set_t *set_new(uint64_t rtype_hash, uint64_t key_index);

bool set_add(n_set_t *m, void *key_ref);

bool set_contains(n_set_t *m, void *key_ref);

void set_delete(n_set_t *m, void *key_ref);

#endif // NATURE_SET_H
