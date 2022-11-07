#include "symbol.h"
#include "utils/helper.h"

void symbol_init() {
    symbol_table = table_new();

    symbol_table_set("print", SYMBOL_TYPE_FN, NULL, false);
    symbol_table_set("println", SYMBOL_TYPE_FN, NULL, false);
    symbol_table_set("debug_printf", SYMBOL_TYPE_FN, NULL, false);
}

// TODO 临时测试使用
bool is_debug_symbol(char *ident) {
    if (str_equal(ident, "print")) {
        return true;
    }
    if (str_equal(ident, "println")) {
        return true;
    }
    if (str_equal(ident, "debug_printf")) {
        return true;
    }
    return false;
}

bool is_print_symbol(char *ident) {
    if (str_equal(ident, "print")) {
        return true;
    }
    if (str_equal(ident, "println")) {
        return true;
    }
    if (str_equal(ident, "builtin_print")) {
        return true;
    }
    return false;
}

// compiler 阶段临时生成的数据
void symbol_table_set_var(char *unique_ident, type_t type) {
    ast_var_decl *var_decl = NEW(ast_var_decl);
    var_decl->type = type;
    var_decl->ident = unique_ident;

    // 添加到符号表中
    symbol_table_set(unique_ident, SYMBOL_TYPE_VAR, var_decl, true);
}

void symbol_table_set(string ident, symbol_type type, void *decl, bool is_local) {
    symbol_t *s = NEW(symbol_t);
    s->ident = ident;
    s->type = type;
    s->value = decl;
    s->is_local = is_local;
    table_set(symbol_table, ident, s);
}

symbol_t *symbol_table_get(char *ident) {
    return table_get(symbol_table, ident);
}

ast_var_decl *symbol_table_get_var(char *ident) {
    symbol_t *s = table_get(symbol_table, ident);
    if (!s) {
        assertf(false, "symbol_table_get_var: symbol not found: %s", ident);
    }
    return s->value;
}


