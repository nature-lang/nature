#ifndef NATURE_BASIC_H
#define NATURE_BASIC_H

#include "utils/type.h"

/**
 * compiler 在面对 number 类型的转换时，其实什么都不需要做？改类型就可以了
 * any 都则需要将 type 和 value 封装在一起并做值替换
 * @param target_index
 * @param value_ref
 */
memory_any_t *convert_any(uint64_t target_index, void *value);

#endif //NATURE_BASIC_H
