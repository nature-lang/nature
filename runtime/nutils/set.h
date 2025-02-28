#ifndef NATURE_SET_H
#define NATURE_SET_H

#include "array.h"
#include "hash.h"
#include "runtime/memory.h"
#include "utils/custom_links.h"
#include "utils/type.h"

#define SET_DEFAULT_CAPACITY 100

n_set_t *rt_set_new(uint64_t rtype_hash, uint64_t key_hash);

bool rt_set_add(n_set_t *m, void *key_ref);

bool rt_set_contains(n_set_t *m, void *key_ref);

void rt_set_delete(n_set_t *m, void *key_ref);

#endif // NATURE_SET_H
