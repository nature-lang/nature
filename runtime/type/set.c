#include "set.h"

static void set_data_index(memory_set_t *m, uint64_t hash_index, uint64_t data_index) {
    uint64_t hash_value = data_index | (1ULL << HASH_SET);

    m->hash_table[hash_index] = hash_value;
}

static void set_grow(memory_set_t *m) {
    DEBUGF("[runtime.set_grow] len=%lu, cap=%lu, key_data=%p, hash_table=%p", m->length, m->capacity, m->key_data,
           m->hash_table);

    rtype_t *key_rtype = rt_find_rtype(m->key_index);
    uint64_t key_size = rtype_heap_out_size(key_rtype, POINTER_SIZE);


    memory_set_t old_set = {0};
    memmove(&old_set, m, sizeof(memory_set_t));

    m->capacity *= 2;
    m->key_data = array_new(key_rtype, m->capacity);
    m->hash_table = runtime_malloc(sizeof(int64_t) * m->capacity, NULL);


    // 对所有的 key 进行 rehash, i 就是 data_index
    for (int data_index = 0; data_index < m->length; ++data_index) {
        void *key_ref = old_set.key_data + data_index * key_size;

        uint64_t hash_index = find_hash_slot(old_set.hash_table,
                                             old_set.capacity,
                                             old_set.key_data,
                                             old_set.key_index,
                                             key_ref);

//        DEBUGF("[runtime.set_grow] find hash_index=%lu by old_set", hash_index);
        if (hash_value_deleted(old_set.hash_table[hash_index])) {
            // 已经删除就不需要再写入到新的 hash table 中嘞
            continue;
        }

        // rehash
        set_add(m, key_ref);
    }
}

memory_set_t *set_new(uint64_t rtype_index, uint64_t key_index) {
    rtype_t *set_rtype = rt_find_rtype(rtype_index);
    rtype_t *key_rtype = rt_find_rtype(key_index);

    memory_set_t *set_data = runtime_malloc(set_rtype->size, set_rtype);
    set_data->capacity = SET_DEFAULT_CAPACITY;
    set_data->length = 0;
    set_data->key_index = key_index;
    set_data->key_data = array_new(key_rtype, set_data->capacity);
    set_data->hash_table = runtime_malloc(sizeof(uint64_t) * set_data->capacity, NULL);

    DEBUGF("[runtime.set_new] success, base=%p,  key_index=%lu, key_data=%p",
           set_data, set_data->key_index, set_data->key_data);
    return set_data;
}

/**
 * 如果值已经存在则返回 false
 * @param m
 * @param key_ref
 * @return
 */
bool set_add(memory_set_t *m, void *key_ref) {
    // 扩容
    if ((double) m->length + 1 > (double) m->capacity * HASH_MAX_LOAD) {
        set_grow(m);
    }


    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_index, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];
//    DEBUGF("[runtime.set_add] hash_index=%lu, hash_value=%lu", hash_index, hash_value)

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

    set_data_index(m, hash_index, key_index);

    uint64_t key_size = rt_rtype_heap_out_size(m->key_index);
    void *dst = m->key_data + key_size * key_index;

//    DEBUGF("[runtime.set_add] key_size=%lu, dst=%p, src=%p", key_size, dst, key_ref);
    memmove(dst, key_ref, key_size);
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

    DEBUGF("[runtime.set_contains] hash_index=%lu, hash_value=%lu", hash_index, hash_value);

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