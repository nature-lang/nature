#include "table.h"
#include "helper.h"
#include "stdlib.h"


static inline void table_adjust(table_t *t, int capacity);

static inline table_entry *table_find_entry(table_entry *entries, int capacity, bool use_hash_key, void *key);

//static uint32_t table_hash_string(const string key) {
//    uint32_t hash = 2166136261u;
//    while (key != NULL && *key != '\0') {
//        hash ^= *key;
//        hash *= 16777619;
//        ++key;
//    }
//
//    return hash;
//}

void table_init(table_t *t) {
    t->count = 0;
    t->capacity = 0;
    t->entries = NULL;
}

void table_free(table_t *t) {
    void *_ = realloc(t, 0);
    table_init(t);
}

void *table_get(table_t *t, void *key) {
    assertf(t, "table is null, called: %s");

    if (t->count == 0) {
        return NULL;
    }

    table_entry *entry = table_find_entry(t->entries, t->capacity, t->use_hash_key, key);
    if (entry->key == NULL) {
        return NULL;
    }

    return entry->value;
}


bool table_set(table_t *t, void *key, void *value) {
    if (t->use_hash_key == false) {
        key = (char *) strdup((char *) key); // 由于需要内部使用，所以不依赖外部的 key
    }

    if (t->count + 1 > t->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(t->capacity);
        table_adjust(t, capacity);
    }

    table_entry *entry = table_find_entry(t->entries, t->capacity, t->use_hash_key, key);

    bool is_new_key = entry->key == NULL;
    if (is_new_key && entry->value == NULL) {
        t->count++;
    }

    entry->key = key;
    entry->value = value;

    return is_new_key;
}


static inline void table_adjust(table_t *t, int capacity) {
    // 创建一个新的 entries 并初始化
    table_entry *entries = mallocz(sizeof(table_entry) * capacity);

    t->count = 0;
    for (int i = 0; i < t->capacity; i++) {
        table_entry *old_entry = &t->entries[i];
        if (old_entry->key == NULL) {
            continue;
        }

        // 重新分配到 new entries，但是依旧要考虑 hash_string 冲突
        table_entry *entry = table_find_entry(entries, capacity, t->use_hash_key, old_entry->key);
        entry->key = old_entry->key;
        entry->value = old_entry->value;
        t->count++;
    }

    // 释放旧 entries
    free(t->entries);
    t->entries = entries; // 修改指针值(而不是指针指向的数据的值)
    t->capacity = capacity;
}

/**
 * 寻找 hash 算法命中的 entry,可能是空桶或者已经存储了元素的桶，但是要需要考虑到开放寻址法
 * tombstone 相当于是一个删除占位符，由于采用了开放寻址法，所以我们无法简单的删除一个 entry。
 * 那会造成开放寻址寻找错误,开放寻址遇到 hash 冲突时会查找线性地址的下一个。删除有可能会造成中断。
 * @param entries
 * @param capacity
 * @param key
 * @return
 */
static inline table_entry *table_find_entry(table_entry *entries, int capacity, bool use_hash_key, void *key) {
    // 计算 hash 值(TODO 可以优化为预计算 hash_string 值)
    uint64_t hash;
    if (use_hash_key) {
        hash = (uint64_t) key;
    } else {
        hash = hash_string(key);
    }

    uint32_t index = hash % capacity;

    table_entry *tombstone = NULL;
    for (;;) {
        table_entry *entry = &entries[index];

        if (entry->key == NULL) {
            if (entry->value == NULL) {
                return tombstone != NULL ? tombstone : entry;
            }

            // key = NULL, value 不为 NULL 表示这是一个被已经被删除的节点,被放置了 tombstone 在次数
            // 所以不只能停止遍历，继续进行开放寻址
            if (tombstone == NULL) {
                tombstone = entry;
            }
        } else {
            if (use_hash_key) {
                if (entry->key == key) {
                    return entry;
                }
            } else {
                if (str_equal((char *) entry->key, (char *) key)) {
                    return entry;
                }
            }
        }

        // 使用开放寻址法解决 hash 冲突解决,如果当前 entry 已经被使用，则使用相邻的下一个 entry
        // 这里再 % 一次是避免值过大溢出
        // 如果 index + 1 < capacity, 则 index + 1 % capacity = index + 1 本身
        // 如果 index + 1 = capacity, 则 index = 0; 既从头开始
        index = (index + 1) % capacity;
    }
}


table_t *table_new(bool use_key_hash) {
    table_t *t = mallocz(sizeof(table_t));
    table_init(t);
    t->use_hash_key = use_key_hash;
    return t;
}

bool table_exist(table_t *t, void *key) {
    assert(t);
    if (t->count == 0) {
        return false;
    }

    table_entry *entry = table_find_entry(t->entries, t->capacity, t->use_hash_key, key);
    if (entry->key == NULL) {
        return false;
    }

    return true;
}

/**
 * 由于使用了开放寻址法，所以不能直接清空 entries，而是需要需要删除的 value 置为 NULL 和 empty 区分开来
 * @param t
 * @param key
 */
void table_delete(table_t *t, void *key) {
    if (t->count == 0) {
        return;
    }

    table_entry *entry = table_find_entry(t->entries, t->capacity, t->use_hash_key, key);
    if (entry->key == NULL) {
        return;
    }

    entry->key = NULL;
    entry->value = (void *) true;
}
