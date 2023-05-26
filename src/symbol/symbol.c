#include "symbol.h"
#include "utils/helper.h"

static symbol_t *_symbol_table_set(string ident, symbol_type_t type, void *ast_value, bool is_local) {
    symbol_t *s = NEW(symbol_t);
    s->ident = ident;
    s->type = type;
    s->ast_value = ast_value;
    s->is_local = is_local;

    if (!is_local && type == SYMBOL_FN && table_exist(symbol_table, ident)) {
        // 对于全局同名函数，首次会进入到 s->ast_value, 其余的则会被作为同名函数进入到 s->global_fndefs
        s = table_get(symbol_table, ident);
        if (!s->global_fndefs) {
            s->global_fndefs = linked_new();
        }
        linked_push(s->global_fndefs, s);
    } else {
        table_set(symbol_table, ident, s);
    }

    return s;
}

void symbol_init() {
    symbol_table = table_new();
    symbol_fn_list = slice_new();
    symbol_var_list = slice_new();
    symbol_typedef_list = slice_new();
}

// compiler 阶段临时生成的数据
void symbol_table_set_var(char *unique_ident, type_t type) {
    ast_var_decl_t *var_decl = NEW(ast_var_decl_t);
    var_decl->type = type;
    var_decl->ident = unique_ident;

    // 添加到符号表中
    symbol_table_set(unique_ident, SYMBOL_VAR, var_decl, true);
}


symbol_t *symbol_table_set(string ident, symbol_type_t type, void *ast_value, bool is_local) {
    symbol_t *s = _symbol_table_set(ident, type, ast_value, is_local);
    if (type == SYMBOL_FN) {
        slice_push(symbol_fn_list, s);
    }

    if (type == SYMBOL_VAR) {
        slice_push(symbol_var_list, s);
    }

    if (type == SYMBOL_TYPEDEF) {
        slice_push(symbol_typedef_list, s);
    }

    return s;
}

symbol_t *symbol_table_get(char *ident) {
    return table_get(symbol_table, ident);
}

ast_var_decl_t *symbol_table_get_var(char *ident) {
    symbol_t *s = table_get(symbol_table, ident);
    if (!s) {
        assertf(false, "symbol_table_get_var: symbol not found: %s", ident);
    }
    return s->ast_value;
}


