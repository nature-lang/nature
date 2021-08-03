#ifndef NATURE_SRC_LIB_TABLE_H_
#define NATURE_SRC_LIB_TABLE_H_

#include "src/value.h"

// hash_string 表写入负载
#define TABLE_MAX_LOAD 0.75

typedef struct {
  string key;
  void *value;
} table_entry;

typedef struct {
  int count;
  int capacity;
  table_entry *entries;
} table;

table *table_new();
void table_init(table *t);
void table_free(table *t);
bool table_exist(table *t, string key);
void *table_get(table *t, string key);
bool table_set(table *t, string key, void *value);
bool table_delete(table *t, string key);

table_entry *table_find_entry(table_entry *entries, int capacity, string key);
void table_adjust(table *t, int capacity);

uint32_t hash_string(const string key);

#endif //NATURE_SRC_LIB_TABLE_H_
