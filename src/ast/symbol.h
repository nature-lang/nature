#ifndef NATURE_SRC_AST_SYMBOL_H_
#define NATURE_SRC_AST_SYMBOL_H_

#include "src/value.h"
#include <stdlib.h>

table var_table;
table custom_type_table;
table function_table;

int64_t list_offset(string type, uint64_t index);

uint64_t type_sizeof(string type);

void symbol_table_init();

/**
 * 符号表存储了变量的作用域，类型，但是绝对是没有值的！
 */

#endif //NATURE_SRC_AST_SYMBOL_H_
