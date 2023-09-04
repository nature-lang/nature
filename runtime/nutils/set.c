#include "set.h"

static void set_data_index(n_set_t *m, uint64_t hash_index, uint64_t data_index) {
    uint64_t hash_value = data_index | (1ULL << HASH_SET);

    m->hash_table[hash_index] = hash_value;
}

// no grow
static bool _set_add(n_set_t *m, void *key_ref) {
    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];
    DEBUGF("[runtime.set_add] hash_index=%lu, hash_value=%lu", hash_index, hash_value)

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

    uint64_t key_size = rt_rtype_out_size(m->key_rtype_hash);
    void *dst = m->key_data + key_size * key_index;

//    DEBUGF("[runtime.set_add] key_size=%lu, dst=%p, src=%p", key_size, dst, key_ref);
    memmove(dst, key_ref, key_size);
    return added;
}

static void set_grow(n_set_t *m) {
    DEBUGF("[runtime.set_grow] len=%lu, cap=%lu, key_data=%p, hash_table=%p", m->length, m->capacity, m->key_data,
           m->hash_table);

    rtype_t *key_rtype = rt_find_rtype(m->key_rtype_hash);
    assertf(key_rtype, "cannot find key_rtype by hash %lu", m->key_rtype_hash);
    uint64_t key_size = rtype_out_size(key_rtype, POINTER_SIZE);


    n_set_t old_set = {0};
    memmove(&old_set, m, sizeof(n_set_t));

    m->capacity *= 2;
    m->key_data = rt_array_new(key_rtype, m->capacity);
    m->hash_table = runtime_malloc(sizeof(int64_t) * m->capacity, NULL);


    // 对所有的 key 进行 rehash, i 就是 data_index
    for (int data_index = 0; data_index < m->length; ++data_index) {
        void *key_ref = old_set.key_data + data_index * key_size;

        uint64_t hash_index = find_hash_slot(old_set.hash_table,
                                             old_set.capacity,
                                             old_set.key_data,
                                             old_set.key_rtype_hash,
                                             key_ref);

//        DEBUGF("[runtime.set_grow] find hash_index=%lu by old_set", hash_index);
        if (hash_value_deleted(old_set.hash_table[hash_index])) {
            // 已经删除就不需要再写入到新的 hash table 中嘞
            continue;
        }

        // rehash
        _set_add(m, key_ref);
    }
}

n_set_t *set_new(uint64_t rtype_hash, uint64_t key_index) {
    runtime_judge_gc();

    rtype_t *set_rtype = rt_find_rtype(rtype_hash);
    rtype_t *key_rtype = rt_find_rtype(key_index);

    n_set_t *set_data = runtime_malloc(set_rtype->size, set_rtype);
    set_data->capacity = SET_DEFAULT_CAPACITY;
    set_data->length = 0;
    set_data->key_rtype_hash = key_index;
    set_data->key_data = rt_array_new(key_rtype, set_data->capacity);
    set_data->hash_table = runtime_malloc(sizeof(uint64_t) * set_data->capacity, NULL);

    DEBUGF("[runtime.set_new] success, base=%p,  key_index=%lu, key_data=%p",
           set_data, set_data->key_rtype_hash, set_data->key_data);
    return set_data;
}

/**
 * 如果值已经存在则返回 false
 * @param m
 * @param key_ref
 * @return
 */
bool set_add(n_set_t *m, void *key_ref) {
    DEBUGF("[runtime.set_add] key_ref=%p, key_rtype_hash=%lu, len=%lu, cap=%lu", key_ref, m->key_rtype_hash, m->length,
           m->capacity);

    runtime_judge_gc();

    // 扩容
    if ((double) m->length + 1 > (double) m->capacity * HASH_MAX_LOAD) {
        set_grow(m);
    }

    return _set_add(m, key_ref);
}

/**
 * 判断 key 是否存在于 set 中
 * @param m
 * @param key_ref
 * @return
 */
bool set_contains(n_set_t *m, void *key_ref) {
    assert(m);
    assert(key_ref);
    assert(m->key_rtype_hash > 0);

    DEBUGF("[runtime.set_contains] key_ref=%p, key_rtype_hash=%lu, len=%lu", key_ref, m->key_rtype_hash, m->length);

    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);
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

void set_delete(n_set_t *m, void *key_ref) {
    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);
    uint64_t *hash_value = &m->hash_table[hash_index];
    *hash_value &= 1ULL << HASH_DELETED; // 配置删除标志即可
    m->length--;
}