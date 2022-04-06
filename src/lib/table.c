#include "table.h"
#include "helper.h"

void table_init(table *t) {
  t->count = 0;
  t->capacity = 0;
  t->entries = NULL;
}

void table_free(table *t) {
  realloc(t, 0);
  table_init(t);
}

void *table_get(table *t, string key) {
  if (t->count == 0) {
    return NULL;
  }

  table_entry *entry = table_find_entry(t->entries, t->capacity, key);
  if (entry->key == NULL) {
    return NULL;
  }

  return entry->value;
}

bool table_set(table *t, char *key, void *value) {
  if (t->count + 1 > t->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(t->capacity);
    table_adjust(t, capacity);
  }

  table_entry *entry = table_find_entry(t->entries, t->capacity, key);

  bool is_new_key = entry->key == NULL;
  if (is_new_key && entry->value == NULL) {
    t->count++;
  }

  entry->key = key;
  entry->value = value;

  return is_new_key;
}

void table_adjust(table *t, int capacity) {
  // 创建一个新的 entries 并初始化
  table_entry *entries = (table_entry *) malloc(sizeof(table_entry) * capacity);
  for (int i = 0; i < capacity; ++i) {
    entries[i].key = NULL;
    entries[i].value = NULL;
  }

  t->count = 0;
  for (int i = 0; i < t->capacity; i++) {
    table_entry *old_entry = &t->entries[i];
    if (old_entry->key == NULL) {
      continue;
    }

    // 重新分配到 new entries，但是依旧要考虑 hash_string 冲突
    table_entry *entry = table_find_entry(entries, capacity, old_entry->key);
    entry->key = old_entry->key;
    entry->value = old_entry->value;
    t->count++;
  }

  // 释放旧 entries
  free(t->entries);
  t->entries = entries; // 修改指针值(而不是指针指向的数据的值)
  t->capacity = capacity;
}

table_entry *table_find_entry(table_entry *entries, int capacity, string key) {
  // 计算 hash 值(TODO 可以优化为预计算 hash_string 值)
  uint32_t hash = hash_string(key);
  uint32_t index = hash % capacity;

  table_entry *tombstone = NULL;
  for (;;) {
    table_entry *entry = &entries[index];

    if (entry->key == NULL) {
      if (entry->value == NULL) {
        return tombstone != NULL ? tombstone : entry;
      }

      // 找到已经被删除的节点时不能停止
      if (tombstone == NULL) {
        tombstone = entry;
      }
    } else if (strequal(entry->key, key)) {
      return entry;
    }

    // 使用开放寻址法解决 hash 冲突解决,如果当前 entry 已经被使用，则使用相邻的下一个 entry
    // 这里再 % 一次是避免值过大溢出
    // 如果 index + 1 < capacity, 则 index + 1 % capacity = index + 1 本身
    // 如果 index + 1 = capacity, 则 index = 0; 既从头开始
    index = (index + 1) % capacity;
  }
}

uint32_t hash_string(const string key) {
  uint32_t hash = 2166136261u;
  while (key != NULL && *key != '\0') {
    hash ^= *key;
    hash *= 16777619;
    ++key;
  }

  return hash;
}

table *table_new() {
  table *t = malloc(sizeof(table));
  table_init(t);
  return t;
}

bool table_exist(table *t, char *key) {
  if (t->count == 0) {
    return false;
  }

  table_entry *entry = table_find_entry(t->entries, t->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  return true;
}



