#include "map.h"
#include "hash.h"
#include "array.h"
#include "utils/links.h"
#include "runtime/memory.h"
#include "runtime/allocator.h"
#include "runtime/runtime.h"

static uint64_t key_hash(rtype_t *rtype, void *key_ref) {
    char *str = ref_to_string_by_rtype(rtype, key_ref);
    return hash_string(str);
}

static bool key_equal(rtype_t *rtype, void *actual, void *expect) {
    char *actual_str = ref_to_string_by_rtype(rtype, actual);
    char *expect_str = ref_to_string_by_rtype(rtype, expect);
    return str_equal(actual_str, expect_str);
}


/**
 * 从 hash table 中找到 value 并返回
 * @param hash_index
 * @return
 */
static uint64_t get_data_index(memory_map_t *m, uint64_t hash_index) {
    uint64_t has_value = m->hash_table[hash_index];

    return extract_data_index(has_value);
}

static void set_data_index(memory_map_t *m, uint64_t hash_index, uint64_t data_index) {
    uint64_t hash_value = data_index & (1ULL << HASH_SET);

    m->hash_table[hash_index] = hash_value;
}

/**
 * 找到当前 key 最合适的 slot 点位(无论是 set 还是 get 使用都必须是否这个点位)
 * @param m
 * @param key_ref
 * @return
 */
static uint64_t find_hash_slot(memory_map_t *m, void *key_ref) {
    // - 计算 hash
    rtype_t *key_rtype = rt_find_rtype(m->key_index);
    uint64_t key_size = rtype_heap_out_size(key_rtype);
    uint64_t hash = key_hash(key_rtype, key_ref);

    // - 开放寻址的方式查找
    uint64_t hash_index = hash % m->capacity;

    // set 操作时就算遇到了一个 deleted slot 也不能直接写入，因为后续可能有当前 key 完全一致的 slot，这是最优先的 set 点
    // 如果知道遍历到 empty slot 都没有找到 key_ref equal 的 slot，那么就可以直接写入遇到的第一个 deleted slot
    int64_t first_deleted_index = -1;
    while (true) {
        uint64_t hash_value = m->hash_table[hash_index];

        // 遇到了 empty 表示当前 key 是第一次插入到 hash 表，此时应该是插入到第一个遇到的 empty 或者 deleted slot
        // 谁先出现就用谁
        if (hash_value_empty(hash_value)) {
            return first_deleted_index != -1 ? first_deleted_index : hash_index;
        }

        if (hash_value_deleted(hash_value) && first_deleted_index == -1) {
            first_deleted_index = (int64_t) hash_index;
        }

        // key equal 的 slot 是最高优先且绝对正确的 slot
        void *actual_key_ref = m->key_data + hash_value * key_size;
        if (key_equal(key_rtype, actual_key_ref, key_ref)) {
            return hash_index;
        }

        hash_index = (hash_index + 1) % m->capacity;
    }
}


void map_grow(memory_map_t *m) {
    rtype_t *key_rtype = rt_find_rtype(m->key_index);
    rtype_t *value_rtype = rt_find_rtype(m->value_index);
    uint64_t key_size = rtype_heap_out_size(key_rtype);
    uint64_t value_size = rtype_heap_out_size(value_rtype);


    memory_map_t old_map;
    memmove(&old_map, m, sizeof(memory_map_t));


    m->capacity *= 2;
    m->key_data = array_new(key_rtype, m->capacity);
    m->value_data = array_new(value_rtype, m->capacity);
    m->hash_table = (void *) runtime_malloc(sizeof(int64_t) * m->capacity, NULL);

    // 对所有的 key 进行 rehash, i 就是 data_index
    for (int data_index = 0; data_index < m->length; ++data_index) {
        void *key_ref = old_map.key_data + data_index * key_size;
        void *value_ref = old_map.value_data + data_index * value_size;

        uint64_t hash_index = find_hash_slot(&old_map, key_ref);
        if (hash_value_deleted(old_map.hash_table[hash_index])) {
            // 已经删除就不需要再写入到新的 hash table 中嘞
            continue;
        }

        // rehash
        map_set(m, key_ref, value_ref);
    }
}


memory_map_t *map_new(uint64_t rtype_index, uint64_t key_index, uint64_t value_index) {
    rtype_t *map_rtype = rt_find_rtype(rtype_index);
    rtype_t *key_rtype = rt_find_rtype(key_index);
    rtype_t *value_rtype = rt_find_rtype(value_index);
    uint64_t capacity = MAP_DEFAULT_CAPACITY;

    rtype_t hash_element_rtype = ct_reflect_type(type_base_new(TYPE_INT));

    // 配置默认值为 -1 表示 empty， -2 作为tombstone,表示 hash 值被删除
    memory_array_t key_data = array_new(key_rtype, capacity);
    memory_array_t value_data = array_new(value_rtype, capacity);

    memory_map_t *map_data = (memory_map_t *) runtime_malloc(map_rtype->size, map_rtype);
    map_data->capacity = capacity;
    map_data->length = 0;
    map_data->key_index = key_index;
    map_data->value_index = value_index;
    map_data->hash_table = (uint64_t *) runtime_malloc(sizeof(uint64_t) * capacity, NULL);
    map_data->key_data = key_data;
    map_data->value_data = value_data;

    return map_data;
}

/**
 * m["key"] = v
 * @param m
 * @param ref
 * @return
 */
void *map_value(memory_map_t *m, void *key_ref) {
    uint64_t hash_index = find_hash_slot(m, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];
    if (hash_value_empty(hash_value) || hash_value_deleted(hash_value)) {
        return NULL;
    }

    uint64_t data_index = get_data_index(m, hash_index);

    // 找到值所在中数组位置起始点并返回
    rtype_t *value_rtype = rt_find_rtype(m->value_index);
    uint64_t value_size = rtype_heap_out_size(value_rtype);

    return m->value_data + value_size * data_index; // 单位字节
}

void *map_set(memory_map_t *m, void *key_ref, void *value_ref) {
    if ((double) m->length + 1 > (double) m->capacity * HASH_MAX_LOAD) {
        map_grow(m);
    }

    uint64_t hash_index = find_hash_slot(m, key_ref);
    uint64_t hash_value = m->hash_table[hash_index];

    uint64_t data_index;
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

    uint64_t key_size = rtype_heap_out_size(rt_find_rtype(m->key_index));
    uint64_t value_size = rtype_heap_out_size(rt_find_rtype(m->value_index));

    // push to key list and value list
    memmove(m->key_data + key_size * data_index, key_ref, key_size);
    memmove(m->value_data + value_size * data_index, value_ref, value_size);
}

/**
 * @param m
 * @param key_ref
 * @return
 */
void *map_delete(memory_map_t *m, void *key_ref) {
    uint64_t hash_index = find_hash_slot(m, key_ref);
    uint64_t *hash_value = &m->hash_table[hash_index];
    *hash_value &= 1ULL << HASH_DELETED; // 配置删除标志即可
    m->length--;
}