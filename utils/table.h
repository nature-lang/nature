#ifndef NATURE_SRC_LIB_TABLE_H_
#define NATURE_SRC_LIB_TABLE_H_

#include "helper.h"

// hash_string 表写入负载
#define TABLE_MAX_LOAD 0.75

typedef struct {
    void *key;

    void *value;
} table_entry;

typedef struct {
    int count;
    int capacity;
    table_entry *entries;
    bool use_hash_key;
} table_t;

table_t *table_new(bool use_key_hash);

void table_init(table_t *t);

void table_free(table_t *t);

bool table_exist(table_t *t, void *key);

void *table_get(table_t *t, void *key);

bool table_set(table_t *t, void *key, void *value);

void table_delete(table_t *t, void *key);

#endif //NATURE_SRC_LIB_TABLE_H_
