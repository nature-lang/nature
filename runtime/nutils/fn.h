#ifndef NATURE_FN_H
#define NATURE_FN_H

#include "utils/mutex.h"
#include "utils/type.h"

extern table_t *env_upvalue_table;
extern mutex_t env_upvalue_locker;


void *fn_new(addr_t fn_addr, envs_t *envs);

envs_t *env_new(uint64_t length);

void *env_element_value(n_fn_t *fn, uint64_t index);

/**
 * 访问的是 env[index] 对应的 addr 中的对应的数据并复制给 dst_ref
 * @param fn_name
 * @param index
 * @param dst_ref
 */
void env_access_ref(n_fn_t *fn, uint64_t index, void *dst_ref, uint64_t size);

/**
 * 这是 env 独有的特殊 assign 方式
 * @param fn_name
 * @param index
 * @param src_ref
 */
void env_assign_ref(n_fn_t *fn, uint64_t index, void *src_ref, uint64_t size);


#endif //NATURE_FN_H
