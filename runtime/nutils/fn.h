#ifndef NATURE_FN_H
#define NATURE_FN_H

#include "utils/type.h"
#include "utils/mutex.h"

#define JIT_CODES_SIZE 80// byte

extern table_t *env_upvalue_table;
extern mutex_t env_upvalue_locker;

typedef struct {
    value_casting value;
    void *ref;
} upvalue_t;

typedef struct {
    void **values;
    uint64_t length;
} envs_t;

typedef struct {
    uint8_t closure_jit_codes[JIT_CODES_SIZE];
    addr_t fn_addr;
    envs_t *envs;
} runtime_fn_t;

void *fn_new(addr_t fn_addr, envs_t *envs);

envs_t *env_new(uint64_t length);

void env_assign(envs_t *envs, uint64_t rtype_hash, uint64_t env_index, addr_t stack_addr);

void env_closure(addr_t stack_addr, uint64_t rtype_hash);

void *env_element_value(runtime_fn_t *fn, uint64_t index);

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


#endif//NATURE_FN_H
