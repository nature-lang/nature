#ifndef NATURE_RUNTIME_HASH_H
#define NATURE_RUNTIME_HASH_H

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
static bool hash_value_set(uint64_t hash_value) {
    uint64_t flag = 1ULL << HASH_SET;
    return hash_value & flag;
}

static bool hash_value_empty(uint64_t hash_value) {
    return !hash_value_set(hash_value);
}

static bool hash_value_deleted(uint64_t hash_value) {
    uint64_t flag = 1ULL << HASH_DELETED;
    return hash_value & flag;
}

/**
 * 右移两位排除掉 hash_empty 和 hash_deleted 就是 data_index 了
 * @param hash_value
 * @return
 */
static bool extract_data_index(uint64_t hash_value) {
    return hash_value << 2;
}


#endif //NATURE_HASH_H
