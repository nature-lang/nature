#ifndef NATURE_SRC_LIB_TABLE_H_
#define NATURE_SRC_LIB_TABLE_H_

#include "helper.h"

// hash_string 表写入负载
#define TABLE_MAX_LOAD 0.75

typedef struct {
    char *key;
    void *value;
} table_entry;

typedef struct {
    int count;
    int capacity;
    table_entry *entries;
} table_t;

table_t *table_new();

void table_init(table_t *t);

static inline void table_free(table_t *t) {
    for (int i = 0; i < t->capacity; i++) {
        if (t->entries[i].key != NULL) {
            free(t->entries[i].key);
        }
    }
    free(t->entries);
    free(t);
}

bool table_exist(table_t *t, string key);

void *table_get(table_t *t, string key);

bool table_set(table_t *t, string key, void *value);

void table_delete(table_t *t, string key);

table_entry *table_find_entry(table_entry *entries, int capacity, string key);

void table_adjust(table_t *t, int capacity);

#endif //NATURE_SRC_LIB_TABLE_H_
