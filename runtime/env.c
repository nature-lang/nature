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

void env_assign(char *fn_name, uint64_t index, addr_t addr) {
    DEBUGF("[runtime.env_assign] fn_name=%s, index=%ld, addr=%lx", fn_name, index, addr);
    assertf(env_table, "env_table is null");
    addr_t *items = table_get(env_table, fn_name);
    assertf(items, "envs=%s not found int table", fn_name);

    items[index] = addr;
}

void env_access_ref(char *fn_name, uint64_t index, void *dst_ref, uint64_t size) {
    assertf(env_table, "env_table is null");
    addr_t *items = table_get(env_table, fn_name);
    assertf(items, "envs=%s not found int table", fn_name);

    // src_ref 是一致地址数据，其指向了 env 引用的变量的栈起始位置
    void *src_ref = (void *) items[index];
    DEBUGF("[runtime.env_access_ref] fn_name=%s, index=%ld, dst=%p, src=%p, size=%lu",
           fn_name,
           index,
           dst_ref,
           src_ref,
           size);


    memmove(dst_ref, src_ref, size);
}

void env_assign_ref(char *fn_name, uint64_t index, void *src_ref, uint64_t size) {
    assertf(env_table, "env_table is null");
    addr_t *items = table_get(env_table, fn_name);
    assertf(items, "envs=%s not found int table", fn_name);

    // src_ref 是一致地址数据，其指向了 env 引用的变量的栈起始位置
    void *dst_ref = (void *) items[index];
    DEBUGF("[runtime.env_assign_ref] fn_name=%s, index=%ld, dst=%p, src=%p, size=%lu",
           fn_name,
           index,
           dst_ref,
           src_ref,
           size);

    memmove(dst_ref, src_ref, size); // ?? size
}

