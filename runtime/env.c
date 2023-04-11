#include "env.h"
#include "stdio.h"
#include "utils/assertf.h"

void env_new(char *fn_name, int capacity) {
    if (env_table == NULL) {
        env_table = table_new();
    }

    // 初始化 env 空间, 直接使用 malloc 即可，一个函数只需要 malloc 一次，所以不会溢出
    void *envs = malloc(sizeof(addr_t) * capacity);
    table_set(env_table, fn_name, envs);
}

void env_access(char *fn_name, uint64_t index, void *value_ref) {
    assertf(env_table, "env_table is null");
    addr_t *items = table_get(env_table, fn_name);
    assertf(items, "envs=%s not found int table", fn_name);

    // src_ref 是一致地址数据，其指向了 env 引用的变量的栈起始位置
    void *src_ref = (void *) items[index];
    memmove(value_ref, src_ref, sizeof(addr_t));
}

