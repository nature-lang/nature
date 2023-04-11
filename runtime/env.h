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

void env_access(char *fn_name, uint64_t index, void *dst_ref);

/**
 * 更新 env 中的值(一旦定义就不会修改了)
 * @param fn_name
 * @param index
 * @param ref
 */
void env_assign(char *fn_name, uint64_t index, void *src_ref);

/**
 * 这是 env 独有的特殊 assign 方式
 * @param fn_name
 * @param index
 * @param src_ref
 */
void env_assign_ref(char *fn_name, uint64_t index, void *src_ref);


#endif //NATURE_ENV_H
