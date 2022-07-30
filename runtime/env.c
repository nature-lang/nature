#include "env.h"
#include "stdio.h"

void env_new(char *env_name, int capacity) {
    if (env_table == NULL) {
        env_table = table_new();
    }

    // 按指针大小初始化
    void **items = malloc(sizeof(void *) * capacity);
    table_set(env_table, env_name, items);
}

void set_env(char *env_name, int index, void *value) {
    if (env_table == NULL) {
        printf("env table not init");
        exit(1);
    }
    void **items = table_get(env_table, env_name);
    if (items == NULL) {
        printf("env %s not found in table", env_name);
        exit(1);
    }
    items[index] = value;
}

void *get_env(char *env_name, int index) {
    if (env_table == NULL) {
        printf("env table not init");
        exit(1);
    }
    void **items = table_get(env_table, env_name);
    if (items == NULL) {
        printf("env %s not found in table", env_name);
        exit(1);
    }

    return items[index];
}

