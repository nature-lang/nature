#ifndef NATURE_MAP_H
#define NATURE_MAP_H

#include "utils/type.h"

#define MAP_DEFAULT_CAPACITY 16 // Java HashMap default 16

n_map_t rt_map_new(uint64_t rtype_hash, uint64_t key_rhash, uint64_t value_rhash);

n_map_t *rt_map_alloc(uint64_t rtype_hash, uint64_t key_rhash, uint64_t value_rhash);

void rt_map_new_out(n_map_t *out, uint64_t rtype_hash, uint64_t key_rhash, uint64_t value_rhash);

uint64_t rt_map_length(n_map_t *l);

void map_grow(n_map_t *m);

n_anyptr_t rt_map_assign(n_map_t *m, void *key_ref);

n_anyptr_t rt_map_access(n_map_t *m, void *key_ref);

void rt_map_delete(n_map_t *m, void *key_ref);

/**
 * 判断 key 是否存在于 set 中
 * @param m
 * @param key_ref
 * @return
 */
bool rt_map_contains(n_map_t *m, void *key_ref);

#endif //NATURE_MAP_H
