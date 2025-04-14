#include "set.h"

static void rt_set_data_index(n_set_t *m, uint64_t hash_index, uint64_t data_index) {
    uint64_t hash_value = data_index | (1ULL << HASH_SET);

    m->hash_table[hash_index] = hash_value;
}

// no grow
static bool _rt_set_add(n_set_t *m, void *key_ref) {
    DEBUGF("[runtime.rt_set_add] key_ref=%p, key_rtype_hash=%lu, len=%lu, cap=%lu", key_ref, m->key_rtype_hash,
           m->length, m->capacity);

    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];
    DEBUGF("[runtime.rt_set_add] hash_index=%lu, hash_value=%lu", hash_index, hash_value);

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

    rt_set_data_index(m, hash_index, key_index);

    uint64_t key_size = rt_rtype_stack_size(m->key_rtype_hash);
    void *dst = m->key_data + key_size * key_index;

    // TODO write barrier

    // DEBUGF("[runtime.set_add] key_size=%lu, dst=%p, src=%p", key_size, dst, key_ref);
    memmove(dst, key_ref, key_size);
    return added;
}

static void rt_set_grow(n_set_t *m) {
    DEBUGF("[runtime.rt_set_grow] len=%lu, cap=%lu, key_data=%p, hash_table=%p", m->length, m->capacity, m->key_data,
           m->hash_table);

    assert(m->key_rtype_hash > 0);
    rtype_t *key_rtype = rt_find_rtype(m->key_rtype_hash);
    assert(key_rtype && "cannot find key_rtype by hash");
    uint64_t key_size = rtype_stack_size(key_rtype, POINTER_SIZE);

    n_set_t old_set = {0};
    memmove(&old_set, m, sizeof(n_set_t));

    m->capacity *= 2;
    m->key_data = rti_array_new(key_rtype, m->capacity);
    m->hash_table = rti_gc_malloc(sizeof(int64_t) * m->capacity, NULL);
    uint64_t len = m->length;
    m->length = 0;// 下面需要重新进行 add 操作

    // 对所有的 key 进行 rehash, i 就是 data_index
    for (int data_index = 0; data_index < len; ++data_index) {
        void *key_ref = old_set.key_data + data_index * key_size;

        uint64_t hash_index = find_hash_slot(old_set.hash_table, old_set.capacity, old_set.key_data,
                                             old_set.key_rtype_hash, key_ref);

        // DEBUGF("[runtime.set_grow] find hash_index=%lu by old_set", hash_index);
        if (hash_value_deleted(old_set.hash_table[hash_index])) {
            // 已经删除就不需要再写入到新的 hash table 中嘞
            continue;
        }

        // rehash
        _rt_set_add(m, key_ref);
    }
}

n_set_t *rt_set_new(uint64_t rtype_hash, uint64_t key_hash) {

    rtype_t *set_rtype = rt_find_rtype(rtype_hash);
    rtype_t *key_rtype = rt_find_rtype(key_hash);

    n_set_t *set_data = rti_gc_malloc(set_rtype->size, set_rtype);
    set_data->capacity = SET_DEFAULT_CAPACITY;
    set_data->length = 0;
    set_data->key_rtype_hash = key_hash;
    set_data->key_data = rti_array_new(key_rtype, set_data->capacity);
    set_data->hash_table = rti_gc_malloc(sizeof(uint64_t) * set_data->capacity, NULL);

    DEBUGF("[runtime.rt_set_new] success, base=%p,  key_hash=%lu, key_data=%p", set_data, set_data->key_rtype_hash,
           set_data->key_data);

    // for (int i = 0; i < set_data->capacity; ++i) {
    //     TDEBUGF("[runtime.set_new] hash_table[%d]=%lu", i, set_data->hash_table[i]);
    // }

    return set_data;
}

/**
 * 如果值已经存在则返回 false
 * @param m
 * @param key_ref
 * @return
 */
bool rt_set_add(n_set_t *m, void *key_ref) {

    DEBUGF("[runtime.rt_set_add] key_ref=%p, key_rtype_hash=%lu, len=%lu, cap=%lu", key_ref, m->key_rtype_hash,
           m->length, m->capacity);

    // 扩容
    if ((double) m->length + 1 > (double) m->capacity * HASH_MAX_LOAD) {
        rt_set_grow(m);
    }

    return _rt_set_add(m, key_ref);
}

/**
 * 判断 key 是否存在于 set 中
 * @param m
 * @param key_ref
 * @return
 */
bool rt_set_contains(n_set_t *m, void *key_ref) {

    assert(m);
    assert(key_ref);
    assert(m->key_rtype_hash > 0);

    DEBUGF("[runtime.rt_set_contains] key_ref=%p, key_rtype_hash=%lu, len=%lu", key_ref, m->key_rtype_hash, m->length);

    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];

    DEBUGF("[runtime.rt_set_contains] hash_index=%lu, hash_value=%lu", hash_index, hash_value);

    if (hash_value_empty(hash_value)) {
        return false;
    }
    if (hash_value_deleted(hash_value)) {
        return false;
    }

    return true;
}

void rt_set_delete(n_set_t *m, void *key_ref) {

    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);
    uint64_t *hash_value = &m->hash_table[hash_index];
    *hash_value &= 1ULL << HASH_DELETED;// 配置删除标志即可
    m->length--;
}