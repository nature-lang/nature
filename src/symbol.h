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
    void *decl; // ast_type_decl_stmt/ast_var_decl/ast_new_fn
} symbol_t;

void symbol_table_set(string ident, symbol_type type, void *decl, bool is_local);

symbol_t *symbol_table_get(string ident);

void symbol_set_temp_ident(string unique_ident, type_t type);

void symbol_init();

bool is_print_symbol(char *ident);

//extern
bool is_debug_symbol(string ident);

#endif //NATURE_SRC_AST_SYMBOL_H_
