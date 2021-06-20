#ifndef NATURE_SRC_AST_SYMBOL_H_
#define NATURE_SRC_AST_SYMBOL_H_

#include "src/value.h"
#include <stdlib.h>

int64_t list_offset(string type, uint64_t index);

uint64_t type_sizeof(string type);

/**
 * 符号表存储了变量的作用域，类型，但是绝对是没有值的！
 */

#endif //NATURE_SRC_AST_SYMBOL_H_
