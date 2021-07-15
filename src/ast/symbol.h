#ifndef NATURE_SRC_AST_SYMBOL_H_
#define NATURE_SRC_AST_SYMBOL_H_

#include "src/value.h"
#include "src/lib/table.h"
#include <stdlib.h>
#include "src/ast/ast.h"

table *symbol_var_table; // analysis_local_var
table *symbol_custom_type_table; // ast_custom_type_decl
table *symbol_function_table; // ast_function_decl

//typedef enum {
//  SYMBOL_TYPE_VAR,
//  SYMBOL_TYPE_CUSTOM_TYPE,
//  SYMBOL_TYPE_FUNCTION,
//} symbol_type;
//
//// 但是进行唯一处理之后，就不需要考虑作用域的问题了？
//typedef struct {
//  symbol_type type;
//  string name;
//} symbol;

//table symbol_table;

int64_t list_offset(string type, uint64_t index);

uint64_t type_sizeof(string type);

void symbol_table_init();

/**
 * 符号表存储了变量的作用域，类型，但是绝对是没有值的！
 */

#endif //NATURE_SRC_AST_SYMBOL_H_
