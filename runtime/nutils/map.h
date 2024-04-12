#ifndef NATURE_MAP_H
#define NATURE_MAP_H

#include "utils/type.h"

#define MAP_DEFAULT_CAPACITY 100

n_map_t *rt_map_new(uint64_t rtype_hash, uint64_t key_index, uint64_t value_index);

uint64_t rt_map_length(n_map_t *l);

void map_grow(n_map_t *m);

n_void_ptr_t rt_map_assign(n_map_t *m, void *key_ref);

n_void_ptr_t rt_map_access(n_map_t *m, void *key_ref);

void rt_map_delete(n_map_t *m, void *key_ref);

#endif //NATURE_MAP_H
