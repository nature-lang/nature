#include "map.h"
#include "array.h"
#include "utils/links.h"
#include "runtime/memory.h"
#include "runtime/allocator.h"

memory_map_t *map_new(uint64_t rtype_index, uint64_t key_index, uint64_t value_index) {
    rtype_t *map_rtype = rt_find_rtype(rtype_index);
    rtype_t *key_rtype = rt_find_rtype(key_index);
    rtype_t *value_rtype = rt_find_rtype(value_index);
    uint64_t capacity = MAP_DEFAULT_CAPACITY;

    rtype_t hash_element_rtype = ct_reflect_type(type_base_new(TYPE_INT));

    memory_array_t hash_data = array_new(&hash_element_rtype, capacity);

    // TODO merged 了怎么计算 rtype 并申请类型？？ 直接搞个 tuple 类型？


    memory_map_t *map_data = (memory_map_t *) runtime_malloc(map_rtype->size, map_rtype);
    map_data->capacity = capacity;
    map_data->length = 0;
    map_data->key_index = key_index;


    return map_data;
}

/**
 * m["key"] = v
 * @param m
 * @param ref
 * @return
 */
memory_map_t *map_value(memory_map_t *m, void *ref) {
    return NULL;
}
