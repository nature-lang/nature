#include "map.h"
#include "array.h"
#include "hash.h"


/**
 * 从 hash table 中找到 value 并返回
 * @param hash_index
 * @return
 */
static uint64_t get_data_index(n_map_t *m, uint64_t hash_index) {
    uint64_t has_value = m->hash_table[hash_index];

    return extract_data_index(has_value);
}

static void set_data_index(n_map_t *m, uint64_t hash_index, uint64_t data_index) {
    uint64_t hash_value = data_index | (1ULL << HASH_SET);

    m->hash_table[hash_index] = hash_value;
}


void map_grow(n_map_t *m) {
    rtype_t *key_rtype = rt_find_rtype(m->key_rtype_hash);
    rtype_t *value_rtype = rt_find_rtype(m->value_rtype_hash);
    uint64_t key_size = key_rtype->stack_size;
    uint64_t value_size = value_rtype->stack_size;

    n_map_t old_map = {0};
    memmove(&old_map, m, sizeof(n_map_t));


    m->length = 0;
    m->capacity *= 2;
    m->key_data = rti_array_new(key_rtype, m->capacity);
    m->value_data = rti_array_new(value_rtype, m->capacity);
    m->hash_table = rti_gc_malloc(sizeof(int64_t) * m->capacity, NULL);

    // 对所有的 key 进行 rehash, i 就是 data_index
    for (int data_index = 0; data_index < old_map.length; ++data_index) {
        void *key_ref = old_map.key_data + data_index * key_size;
        void *value_ref = old_map.value_data + data_index * value_size;

        uint64_t hash_index = find_hash_slot(old_map.hash_table,
                                             old_map.capacity,
                                             old_map.key_data,
                                             old_map.key_rtype_hash,
                                             key_ref);
        if (hash_value_deleted(old_map.hash_table[hash_index])) {
            // 已经删除就不需要再写入到新的 hash table 中嘞
            continue;
        }

        // rehash
        void *new_value_ref = (void *) rt_map_assign(m, key_ref); // 这里会导致 m length 变长
        memmove(new_value_ref, value_ref, value_size);
    }
}


n_map_t *rt_map_new(uint64_t rtype_hash, uint64_t key_rhash, uint64_t value_rhash) {
    rtype_t *map_rtype = rt_find_rtype(rtype_hash);
    rtype_t *key_rtype = rt_find_rtype(key_rhash);
    rtype_t *value_rtype = rt_find_rtype(value_rhash);
    uint64_t capacity = MAP_DEFAULT_CAPACITY;
    DEBUGF("[runtime.rt_map_new] map_rhash=%ld(%s-%ld), key_rhash=%ld(%s-%ld), value_rindex=%ld(%s-%ld)",
           rtype_hash,
           type_kind_str[map_rtype->kind],
           map_rtype->heap_size,
           key_rhash,
           type_kind_str[key_rtype->kind],
           key_rtype->heap_size,
           value_rhash,
           type_kind_str[value_rtype->kind],
           value_rtype->heap_size);

    n_map_t *map_data = rti_gc_malloc(map_rtype->heap_size, map_rtype);
    map_data->capacity = capacity;
    map_data->length = 0;
    map_data->key_rtype_hash = key_rhash;
    map_data->value_rtype_hash = value_rhash;
    map_data->hash_table = rti_gc_malloc(sizeof(uint64_t) * capacity, NULL);
    map_data->key_data = rti_array_new(key_rtype, capacity);
    map_data->value_data = rti_array_new(value_rtype, capacity);

    return map_data;
}

/**
 * m["key"] = v
 * @param m
 * @param key_ref
 * @return false 表示没有找到响应的值，也就是值不存在, true 表示相关值已经 copy 到了 value_ref 中
 */
n_anyptr_t rt_map_access(n_map_t *m, void *key_ref) {
    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);

    rtype_t *key_rtype = rt_find_rtype(m->key_rtype_hash);
    char *key_str = rtype_value_to_str(key_rtype, key_ref);
    DEBUGF("[runtime.rt_map_access] key_rtype_kind: %d, key_str: %s, hash_index=%lu,", key_rtype->kind, key_str,
           hash_index);

    uint64_t hash_value = m->hash_table[hash_index];
    if (hash_value_empty(hash_value) || hash_value_deleted(hash_value)) {
        DEBUGF("[runtime.rt_map_access] hash value=%lu, empty=%d, deleted=%d",
               hash_value,
               hash_value_empty(hash_value),
               hash_value_deleted(hash_value));

        char *msg = tlsprintf("key '%s' not found in map", key_str);
        rti_throw(msg, true);
        return 0;
    }

    free((void *) key_str);
    uint64_t data_index = get_data_index(m, hash_index);

    // 找到值所在中数组位置起始点并返回
    uint64_t value_size = rt_rtype_stack_size(m->value_rtype_hash);

    DEBUGF("[runtime.rt_map_access] value_base=%p, find hash_value=%lu,data_index=%lu,value_size=%lu success",
           m->value_data,
           hash_value,
           data_index,
           value_size);

    void *src = m->value_data + value_size * data_index; // 单位字节
    return (n_anyptr_t) src;
}

n_anyptr_t rt_map_assign(n_map_t *m, void *key_ref) {
    if ((double) m->length + 1 > (double) m->capacity * HASH_MAX_LOAD) {
        map_grow(m);
    }

    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];

    rtype_t *key_rtype = rt_find_rtype(m->key_rtype_hash);

    //    char *key_str = rtype_value_to_str(key_rtype, key_ref);
    //
    //    DEBUGF("[runtime.rt_map_assign] key_rtype_kind=%d, key_str=%s, hash_index=%lu, map_len=%d",
    //           key_rtype->kind,
    //           key_str,
    //           hash_index,
    //           m->length);
    //
    //    free((void *) key_str);

    uint64_t data_index = 0;
    if (hash_value_empty(hash_value)) {
        data_index = m->length++;
    } else if (hash_value_deleted(hash_value)) {
        m->length++;
        data_index = extract_data_index(hash_value);
    } else {
        // 绝对的修改
        data_index = extract_data_index(hash_value);
    }

    set_data_index(m, hash_index, data_index);

    uint64_t key_size = rt_rtype_stack_size(m->key_rtype_hash);
    uint64_t value_size = rt_rtype_stack_size(m->value_rtype_hash);
    DEBUGF("[runtime.rt_map_assign] assign success data_index=%lu, hash_value=%lu, key_size=%ld",
           data_index,
           m->hash_table[hash_index], key_size);

    if (key_rtype->heap_size == POINTER_SIZE) {
        rti_write_barrier_ptr(m->key_data + key_size * data_index, *(void **) key_ref, false);
    } else {
        // push to key list and value list
        memmove(m->key_data + key_size * data_index, key_ref, key_size);
    }

    return (n_anyptr_t) (m->value_data + value_size * data_index);
}

/**
 * @param m
 * @param key_ref
 * @return
 */
void rt_map_delete(n_map_t *m, void *key_ref) {
    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);
    uint64_t *hash_value = &m->hash_table[hash_index];
    *hash_value &= 1ULL << HASH_DELETED; // 配置删除标志即可
    m->length--;
}

uint64_t rt_map_length(n_map_t *l) {

    return l->length;
}

/**
 * 判断 key 是否存在于 set 中
 * @param m
 * @param key_ref
 * @return
 */
bool rt_map_contains(n_map_t *m, void *key_ref) {
    assert(m);
    assert(key_ref);
    assert(m->key_rtype_hash > 0);

    DEBUGF("[runtime.rt_map_contains] key_ref=%p, key_rtype_hash=%lu, len=%lu", key_ref, m->key_rtype_hash, m->length);

    uint64_t hash_index = find_hash_slot(m->hash_table, m->capacity, m->key_data, m->key_rtype_hash, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];

    DEBUGF("[runtime.rt_map_contains] hash_index=%lu, hash_value=%lu", hash_index, hash_value);

    if (hash_value_empty(hash_value)) {
        return false;
    }
    if (hash_value_deleted(hash_value)) {
        return false;
    }

    return true;
}
