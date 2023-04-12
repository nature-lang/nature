#ifndef NATURE_ENV_H
#define NATURE_ENV_H

#include "utils/table.h"

table_t *env_table;

/**
 * env 中保存的是 addr_t, 所以占用固定空间 8byte
 * @param fn_name
 * @param capacity
 */
void env_new(char *fn_name, int capacity);

/**
 * 更新 env 中的值
 * @param fn_name
 * @param index
 * @param ref
 */
void env_assign(char *fn_name, uint64_t index, addr_t src_ref);


/**
 * 访问的是 env[index] 对应的 addr 中的对应的数据并复制给 dst_ref
 * @param fn_name
 * @param index
 * @param dst_ref
 */
void env_access_ref(char *fn_name, uint64_t index, void *dst_ref, uint64_t size);

/**
 * 这是 env 独有的特殊 assign 方式
 * @param fn_name
 * @param index
 * @param src_ref
 */
void env_assign_ref(char *fn_name, uint64_t index, void *src_ref, uint64_t size);


#endif //NATURE_ENV_H
