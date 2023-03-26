#ifndef NATURE_SRC_LIB_TABLE_H_
#define NATURE_SRC_LIB_TABLE_H_

#include "value.h"

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
} table_t;

table_t *table_new();

void table_init(table_t *t);

void table_free(table_t *t);

bool table_exist(table_t *t, string key);

void *table_get(table_t *t, string key);

bool table_set(table_t *t, string key, void *value);

void table_delete(table_t *t, string key);

table_entry *table_find_entry(table_entry *entries, int capacity, string key);

void table_adjust(table_t *t, int capacity);

#endif //NATURE_SRC_LIB_TABLE_H_
