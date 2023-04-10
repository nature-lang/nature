#ifndef NATURE_MAP_H
#define NATURE_MAP_H

#include "utils/type.h"

#define MAP_DEFAULT_CAPACITY 100

memory_map_t *map_new(uint64_t rtype_index, uint64_t key_index, uint64_t value_index);

bool map_access(memory_map_t *m, void *key_ref, void *value_ref);

uint64_t map_length(memory_map_t *l);

void map_grow(memory_map_t *m);

void map_assign(memory_map_t *m, void *key_ref, void *value_ref);

void map_delete(memory_map_t *m, void *key_ref);

#endif //NATURE_MAP_H
