#ifndef NATURE_RUNTIME_HASH_H
#define NATURE_RUNTIME_HASH_H

#include "runtime/runtime.h"
#include "runtime/memory.h"
#include "utils/type.h"

// hash table 的最高位(64)如果为 1 则表示其有值
// hash table 的次高位如果为 1 则表示其被删除
#define HASH_SET 63
#define HASH_DELETED 62

#define HASH_MAX_LOAD 0.75

/**
 * 1 表示 set, 0 表示 emtpy
 * @param hash_value
 * @return
 */
static inline bool hash_value_set(uint64_t hash_value) {
    uint64_t flag = 1ULL << HASH_SET;
    return hash_value & flag;
}

static inline bool hash_value_empty(uint64_t hash_value) {
    return !hash_value_set(hash_value);
}

static inline bool hash_value_deleted(uint64_t hash_value) {
    uint64_t flag = 1ULL << HASH_DELETED;
    return hash_value & flag;
}

/**
 * 右移两位排除掉 hash_empty 和 hash_deleted 就是 data_index 了
 * @param hash_value
 * @return
 */
static inline uint64_t extract_data_index(uint64_t hash_value) {
    return hash_value << 2 >> 2;
}


static inline uint64_t key_hash(rtype_t *rtype, void *key_ref) {
    char *str = rtype_value_str(rtype, key_ref);
    return hash_string(str);
}

static inline bool key_equal(rtype_t *rtype, void *actual, void *expect) {
    DEBUGF("[key_equal] actual=%p, expect=%p", actual, expect)
    char *actual_str = rtype_value_str(rtype, actual);
    char *expect_str = rtype_value_str(rtype, expect);
    return str_equal(actual_str, expect_str);
}

/**
 * 找到当前 key 最合适的 slot 点位(无论是 set 还是 get 使用都必须是否这个点位)
 * @param m
 * @param key_ref
 * @return
 */
static inline uint64_t
find_hash_slot(uint64_t *hash_table, uint64_t capacity, uint8_t *key_data, uint64_t key_rtype_hash, void *key_ref) {
    // - 计算 hash
    rtype_t *key_rtype = rt_find_rtype(key_rtype_hash);
    assertf(key_rtype, "cannot find rtype by hash=%d", key_rtype_hash);
    DEBUGF("[find_hash_slot] key_ref=%p,  key type_kind=%s", key_ref, type_kind_str[key_rtype->kind])

    uint64_t key_size = rtype_out_size(key_rtype, POINTER_SIZE);
    uint64_t hash = key_hash(key_rtype, key_ref);

    // - 开放寻址的方式查找
    uint64_t hash_index = hash % capacity;

    // set 操作时就算遇到了一个 deleted slot 也不能直接写入，因为后续可能有当前 key 完全一致的 slot，这是最优先的 set 点
    // 如果知道遍历到 empty slot 都没有找到 key_ref equal 的 slot，那么就可以直接写入遇到的第一个 deleted slot
    int64_t first_deleted_index = -1;
    while (true) {
        uint64_t hash_value = hash_table[hash_index];
        uint64_t key_index = extract_data_index(hash_value);
        DEBUGF("[find_hash_slot] key_data=%p, key_size=%ld, hash_index=%lu, hash_value=%lu, key_index=%lu",
               key_data, key_size, hash_index, hash_value, key_index)


        // 遇到了 empty 表示当前 key 是第一次插入到 hash 表，此时应该是插入到第一个遇到的 empty 或者 deleted slot
        // 谁先出现就用谁
        if (hash_value_empty(hash_value)) {
            return first_deleted_index != -1 ? first_deleted_index : hash_index;
        }

        if (hash_value_deleted(hash_value) && first_deleted_index == -1) {
            first_deleted_index = (int64_t) hash_index;
        }

        // key equal 的 slot 是最高优先且绝对正确的 slot
        void *actual_key_ref = key_data + key_index * key_size;
        if (key_equal(key_rtype, actual_key_ref, key_ref)) {
            return hash_index;
        }

        hash_index = (hash_index + 1) % capacity;
    }
}


#endif //NATURE_HASH_H
