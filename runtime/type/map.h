#ifndef NATURE_MAP_H
#define NATURE_MAP_H

#include "utils/type.h"

#define MAP_DEFAULT_CAPACITY 100

memory_map_t *map_new(uint64_t rtype_index, uint64_t key_index, uint64_t value_index);

void *map_value(memory_map_t *m, void *ref);

#endif //NATURE_MAP_H
