#ifndef NATURE_SRC_AST_SYMBOL_H_
#define NATURE_SRC_AST_SYMBOL_H_

#include <stdlib.h>
#include "src/value.h"
#include "utils/table.h"
#include "ast.h"

#define SYMBOL_GET_VAR_DECL(ident) \
({                                   \
    analysis_local_ident *local_ident = table_get(symbol_ident_table, ident); \
    (ast_var_decl*) local_ident->decl; \
})

// 局部变量也会添加到全局符号表中，但是不同 closure 允许存在相同的变量名称，不同 module 则允许相同的 closure
// 所以 symbol_ident_table 的 key 是 module_name + closure_name + ident
// 如果是 closure 则就是 module_name + closure
table_t *symbol_table; // analysis_local_ident_t

typedef enum {
    SYMBOL_TYPE_VAR,
    SYMBOL_TYPE_CUSTOM,
    SYMBOL_TYPE_FN,
} symbol_type;

typedef struct {
    string ident;
    symbol_type type;
    bool is_local; // 是否为本地符号
    void *value; // ast_type_decl_stmt/ast_var_decl/ast_new_fn
} symbol_t;

void symbol_table_set(string ident, symbol_type type, void *decl, bool is_local);

symbol_t *symbol_table_get(string ident);

void symbol_table_set_var(string unique_ident, type_t type);

ast_var_decl *symbol_table_get_var(string ident);

void symbol_init();

bool is_print_symbol(char *ident);

//extern
bool is_debug_symbol(string ident);

#endif //NATURE_SRC_AST_SYMBOL_H_
