#include "env.h"
#include "stdio.h"
#include "utils/assertf.h"

void env_new(char *env_name, int capacity) {
    if (env_table == NULL) {
        env_table = table_new();
    }

    // 按指针大小初始化
    void **items = malloc(sizeof(void *) * capacity);
    table_set(env_table, env_name, items);
}

void set_env(char *env_name, int index, void *value) {
    assertf(env_table, "env_table is null");
    void **items = table_get(env_table, env_name);
    assertf(items, "env list %s not found int table", env_name);
    items[index] = value;
}

void *env_value(char *env_name, int index) {
    assertf(env_name, "env_name is null");
    assertf(env_table, "env_table is null");
    void **items = table_get(env_table, env_name);
    assertf(items, "env list %s not found int table", env_name);

    return items[index];
}

