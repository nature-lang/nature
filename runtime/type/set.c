#include "set.h"

memory_set_t *set_new(uint64_t rtype_index, uint64_t key_index) {
    rtype_t *set_rtype = rt_find_rtype(rtype_index);
    rtype_t *key_rtype = rt_find_rtype(key_index);

    memory_set_t *set_data = runtime_malloc(set_rtype->size, set_rtype);
    set_data->capacity = SET_DEFAULT_CAPACITY;
    set_data->length = 0;
    set_data->key_index = key_index;
    set_data->key_data = array_new(key_rtype, set_data->capacity);

    return set_data;
}

/**
 * 如果值已经存在则返回 false
 * @param m
 * @param key_ref
 * @return
 */
bool set_add(memory_set_t *m, void *key_ref) {
    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_index, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];

    uint64_t key_index;
    bool added = false;
    if (hash_value_empty(hash_value)) {
        key_index = m->length++;
        added = true;
    } else if (hash_value_deleted(hash_value)) {
        m->length++;
        added = true;
        key_index = extract_data_index(hash_value);
    } else {
        // 绝对的修改
        key_index = extract_data_index(hash_value);
    }
    uint64_t key_size = rt_rtype_heap_out_size(m->key_index);
    memmove(m->key_data + key_size * key_index, key_ref, key_size);

    return added;
}

/**
 * 判断 key 是否存在于 set 中
 * @param m
 * @param key_ref
 * @return
 */
bool set_contains(memory_set_t *m, void *key_ref) {
    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_index, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];

    if (hash_value_empty(hash_value)) {
        return false;
    }
    if (hash_value_deleted(hash_value)) {
        return false;
    }

    return true;
}

void set_delete(memory_set_t *m, void *key_ref) {
    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_index, key_ref);
    uint64_t *hash_value = &m->hash_table[hash_index];
    *hash_value &= 1ULL << HASH_DELETED; // 配置删除标志即可
    m->length--;
}