#ifndef NATURE_MAP_H
#define NATURE_MAP_H

#include "utils/type.h"

#define MAP_DEFAULT_CAPACITY 100

n_map_t *map_new(uint64_t rtype_hash, uint64_t key_index, uint64_t value_index);

bool map_access(n_map_t *m, void *key_ref, void *value_ref);

uint64_t map_length(n_map_t *l);

void map_grow(n_map_t *m);

void map_assign(n_map_t *m, void *key_ref, void *value_ref);

void map_delete(n_map_t *m, void *key_ref);

#endif //NATURE_MAP_H
