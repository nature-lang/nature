#include "symbol.h"
#include "utils/helper.h"

// 临时表，用来临时记录, key = ident, value is any
table_t *can_import_symbol_table;

table_t *import_temp_symbol_table;


/**
 * 编译时产生的所有符号都进行唯一处理后写入到该 table 中
 * 1. 模块名 + fn名称
 * 2. 作用域不同时允许同名的符号(局部变量)，也进行唯一性处理
 *
 * 符号的来源有
 * 1. 局部变量与全局变量
 * 2. 函数
 * 3. 自定义 type, 例如 type foo = int
 */
table_t *symbol_table;

slice_t *symbol_fn_list;

slice_t *symbol_closure_list;

slice_t *symbol_var_list;

slice_t *symbol_typedef_list;

static symbol_t *_symbol_table_set(string ident, symbol_type_t type, void *ast_value, bool is_local) {
    symbol_t *s = table_get(symbol_table, ident);

    // 符号重复注册
    if (s) {
        assertf(type == SYMBOL_FN, "symbol=%s duplicate set", ident);
        assert(s->fndefs);
        assert(s->fndefs->count > 0);
        slice_push(s->fndefs, ast_value);
        return s;
    }

    s = NEW(symbol_t);
    s->ident = ident;
    s->type = type;
    s->ast_value = ast_value;
    s->is_local = is_local;
    if (type == SYMBOL_FN) {
        s->fndefs = slice_new();
        slice_push(s->fndefs, ast_value);
    }

    table_set(symbol_table, ident, s);

    return s;
}

void symbol_init() {
    can_import_symbol_table = table_new();
    import_temp_symbol_table = table_new();

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

    if (type == SYMBOL_TYPE_ALIAS) {
        slice_push(symbol_typedef_list, s);
    }

    return s;
}


symbol_t *symbol_table_get_noref(char *ident) {
    return table_get(symbol_table, ident);
}

symbol_t *symbol_table_get(char *ident) {
    symbol_t *s = table_get(symbol_table, ident);
    if (!s) {
        return NULL;
    }

    // 引用计数
    s->ref_count += 1;
    table_set(symbol_table, ident, s);

    return s;
}



