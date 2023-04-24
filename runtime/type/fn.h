#ifndef NATURE_FN_H
#define NATURE_FN_H

#include "utils/type.h"


table_t *env_table;

typedef struct {
    value_casting value;
    void *ref;
} upvalue_t;

typedef struct {
    upvalue_t **values;
    uint64_t length;
} envs_t;

typedef struct {
    envs_t *envs;
    uint8_t *closure_jit_codes;
    addr_t fn_addr;
    uint64_t code_size;
} runtime_fn_t;

void *fn_new(addr_t fn_addr, envs_t *envs);

envs_t *env_new(int length);

void env_assign(envs_t *envs, uint64_t item_rtype_index, uint64_t index, addr_t stack_addr);

void env_close(addr_t stack_addr);

/**
 * 访问的是 env[index] 对应的 addr 中的对应的数据并复制给 dst_ref
 * @param fn_name
 * @param index
 * @param dst_ref
 */
void env_access_ref(runtime_fn_t *fn, uint64_t index, void *dst_ref, uint64_t size);

/**
 * 这是 env 独有的特殊 assign 方式
 * @param fn_name
 * @param index
 * @param src_ref
 */
void env_assign_ref(runtime_fn_t *fn, uint64_t index, void *src_ref, uint64_t size);


#endif //NATURE_FN_H
