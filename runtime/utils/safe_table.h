#ifndef NATURE_UTILS_SAFE_TABLE_H
#define NATURE_UTILS_SAFE_TABLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "runtime/runtime.h"
#include "safe_mutex.h"
#include "utils/table.h"

static inline table_t *safe_table_new() {
    PREEMPT_LOCK()
    table_t *table = table_new();
    PREEMPT_UNLOCK()
    return table;
}

static inline bool safe_table_exist(table_t *table, string key) {
    PREEMPT_LOCK()
    bool result = table_exist(table, key);
    PREEMPT_UNLOCK()
    return result;
}

static inline void *safe_table_get(table_t *table, string key) {
    PREEMPT_LOCK()
    void *result = table_get(table, key);
    PREEMPT_UNLOCK()
    return result;
}

static inline bool safe_table_set(table_t *table, string key, void *value) {
    PREEMPT_LOCK()
    bool result = table_set(table, key, value);
    PREEMPT_UNLOCK()
    return result;
}

#endif // NATURE_BITMAP_H
