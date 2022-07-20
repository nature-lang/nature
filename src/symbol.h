#ifndef NATURE_SRC_AST_SYMBOL_H_
#define NATURE_SRC_AST_SYMBOL_H_

#include <stdlib.h>
#include "src/value.h"
#include "src/lib/table.h"
#include "ast.h"

#define SYMBOL_GET_VAR_DECL(ident) \
({                                   \
    analysis_local_ident *local_ident = table_get(symbol_ident_table, ident); \
    (ast_var_decl*) local_ident->decl; \
})

table *symbol_ident_table; // analysis_local_ident

typedef enum {
    SYMBOL_TYPE_VAR,
    SYMBOL_TYPE_CUSTOM_TYPE,
//  SYMBOL_TYPE_FUNCTION,
} symbol_type;

void symbol_set_temp_ident(string unique_ident, ast_type type);

int64_t list_offset(ast_type type, uint64_t index);

size_t struct_offset(ast_struct_decl *struct_decl, string property);

/**
 * 单位 byte
 * 字符串类型，list 类型， map 类型,都按照指针来计算大小
 * @param type
 * @return
 */
size_t type_sizeof(ast_type type);

void symbol_ident_table_init();

// 添加内置函数
void symbol_set_builtin_ident();
//extern
bool is_extern_symbol(string ident);


#endif //NATURE_SRC_AST_SYMBOL_H_
