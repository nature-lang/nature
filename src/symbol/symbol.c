#include "symbol.h"
#include "utils/helper.h"

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

static ast_typedef_stmt_t *builtin_typedef_base(char *ident) {
    ast_typedef_stmt_t *stmt = NEW(ast_typedef_stmt_t);
    sc_map_init_sv(&stmt->method_table, 0, 0);
    stmt->ident = ident;
    stmt->params = NULL;
    stmt->type_expr = type_kind_new(TYPE_UNKNOWN);
    stmt->is_alias = false;
    stmt->is_interface = false;
    stmt->is_enum = false;
    stmt->is_tagged_union = false;
    stmt->impl_interfaces = NULL;
    stmt->hash = 0;
    return stmt;
}

static list_t *builtin_params_1(char *ident) {
    list_t *params = ct_list_new(sizeof(ast_generics_param_t));
    ast_generics_param_t *param = ast_generics_param_new(0, 0, ident);
    ct_list_push(params, param);
    return params;
}

static list_t *builtin_params_2(char *ident1, char *ident2) {
    list_t *params = ct_list_new(sizeof(ast_generics_param_t));
    ast_generics_param_t *param1 = ast_generics_param_new(0, 0, ident1);
    ast_generics_param_t *param2 = ast_generics_param_new(0, 0, ident2);
    ct_list_push(params, param1);
    ct_list_push(params, param2);
    return params;
}

static ast_typedef_stmt_t *builtin_typedef_simple(char *ident, type_kind kind) {
    ast_typedef_stmt_t *stmt = builtin_typedef_base(ident);
    stmt->type_expr = type_kind_new(kind);
    return stmt;
}

static ast_typedef_stmt_t *builtin_typedef_vec(char *ident) {
    ast_typedef_stmt_t *stmt = builtin_typedef_base(ident);
    stmt->params = builtin_params_1(TYPE_PARAM_T);

    type_t t = type_kind_new(TYPE_VEC);
    t.vec = NEW(type_vec_t);
    t.vec->element_type = type_ident_new(TYPE_PARAM_T, TYPE_IDENT_GENERICS_PARAM);
    stmt->type_expr = t;
    return stmt;
}

static ast_typedef_stmt_t *builtin_typedef_set(char *ident) {
    ast_typedef_stmt_t *stmt = builtin_typedef_base(ident);
    stmt->params = builtin_params_1(TYPE_PARAM_T);

    type_t t = type_kind_new(TYPE_SET);
    t.set = NEW(type_set_t);
    t.set->element_type = type_ident_new(TYPE_PARAM_T, TYPE_IDENT_GENERICS_PARAM);
    stmt->type_expr = t;
    return stmt;
}

static ast_typedef_stmt_t *builtin_typedef_chan(char *ident) {
    ast_typedef_stmt_t *stmt = builtin_typedef_base(ident);
    stmt->params = builtin_params_1(TYPE_PARAM_T);

    type_t t = type_kind_new(TYPE_CHAN);
    t.chan = NEW(type_chan_t);
    t.chan->element_type = type_ident_new(TYPE_PARAM_T, TYPE_IDENT_GENERICS_PARAM);
    stmt->type_expr = t;
    return stmt;
}

static ast_typedef_stmt_t *builtin_typedef_map(char *ident) {
    ast_typedef_stmt_t *stmt = builtin_typedef_base(ident);
    stmt->params = builtin_params_2(TYPE_PARAM_T, TYPE_PARAM_U);

    type_t t = type_kind_new(TYPE_MAP);
    t.map = NEW(type_map_t);
    t.map->key_type = type_ident_new(TYPE_PARAM_T, TYPE_IDENT_GENERICS_PARAM);
    t.map->value_type = type_ident_new(TYPE_PARAM_U, TYPE_IDENT_GENERICS_PARAM);
    stmt->type_expr = t;
    return stmt;
}

static ast_typedef_stmt_t *builtin_typedef_tuple(char *ident) {
    ast_typedef_stmt_t *stmt = builtin_typedef_base(ident);
    type_tuple_t *tuple = NEW(type_tuple_t);
    tuple->elements = ct_list_new(sizeof(type_t));
    stmt->type_expr = type_new(TYPE_TUPLE, tuple);
    return stmt;
}

static void symbol_register_builtin_types() {
    symbol_table_set("bool", SYMBOL_TYPE, builtin_typedef_simple("bool", TYPE_BOOL), false);
    symbol_table_set("int", SYMBOL_TYPE, builtin_typedef_simple("int", TYPE_INT), false);
    symbol_table_set("uint", SYMBOL_TYPE, builtin_typedef_simple("uint", TYPE_UINT), false);
    symbol_table_set("float", SYMBOL_TYPE, builtin_typedef_simple("float", TYPE_FLOAT), false);
    symbol_table_set("i8", SYMBOL_TYPE, builtin_typedef_simple("i8", TYPE_INT8), false);
    symbol_table_set("i16", SYMBOL_TYPE, builtin_typedef_simple("i16", TYPE_INT16), false);
    symbol_table_set("i32", SYMBOL_TYPE, builtin_typedef_simple("i32", TYPE_INT32), false);
    symbol_table_set("i64", SYMBOL_TYPE, builtin_typedef_simple("i64", TYPE_INT64), false);
    symbol_table_set("u8", SYMBOL_TYPE, builtin_typedef_simple("u8", TYPE_UINT8), false);
    symbol_table_set("u16", SYMBOL_TYPE, builtin_typedef_simple("u16", TYPE_UINT16), false);
    symbol_table_set("u32", SYMBOL_TYPE, builtin_typedef_simple("u32", TYPE_UINT32), false);
    symbol_table_set("u64", SYMBOL_TYPE, builtin_typedef_simple("u64", TYPE_UINT64), false);
    symbol_table_set("f32", SYMBOL_TYPE, builtin_typedef_simple("f32", TYPE_FLOAT32), false);
    symbol_table_set("f64", SYMBOL_TYPE, builtin_typedef_simple("f64", TYPE_FLOAT64), false);
    symbol_table_set("string", SYMBOL_TYPE, builtin_typedef_simple("string", TYPE_STRING), false);

    symbol_table_set("vec", SYMBOL_TYPE, builtin_typedef_vec("vec"), false);
    symbol_table_set("set", SYMBOL_TYPE, builtin_typedef_set("set"), false);
    symbol_table_set("map", SYMBOL_TYPE, builtin_typedef_map("map"), false);
    symbol_table_set("chan", SYMBOL_TYPE, builtin_typedef_chan("chan"), false);
    symbol_table_set("tup", SYMBOL_TYPE, builtin_typedef_tuple("tup"), false);
}

static symbol_t *_symbol_table_set(string ident, symbol_type_t type, void *ast_value, bool is_local) {
    symbol_t *s = NEW(symbol_t);
    s->ident = ident;
    s->type = type;
    s->ast_value = ast_value;
    s->is_local = is_local;

    table_set(symbol_table, ident, s);

    return s;
}

void symbol_init() {
    symbol_table = table_new();
    symbol_fn_list = slice_new();
    symbol_var_list = slice_new();
    symbol_typedef_list = slice_new();

    symbol_register_builtin_types();
}

symbol_t *symbol_table_set(string ident, symbol_type_t type, void *ast_value, bool is_local) {
    bool overlay = table_exist(symbol_table, ident);

    symbol_t *s = _symbol_table_set(ident, type, ast_value, is_local);
    if (type == SYMBOL_FN) {
        slice_push(symbol_fn_list, s);
    }

    if (type == SYMBOL_VAR) {
        slice_push(symbol_var_list, s);
    }

    if (type == SYMBOL_TYPE) {
        slice_push(symbol_typedef_list, s);
    }

    if (overlay) {
        return NULL;
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

symbol_t *symbol_typedef_add_method(char *typedef_ident, char *method_ident, ast_fndef_t *fndef) {
    symbol_t *symbol = symbol_table_get(typedef_ident);
    assert(symbol);
    assert(symbol->type == SYMBOL_TYPE);
    ast_typedef_stmt_t *typedef_stmt = symbol->ast_value;

    sc_map_put_sv(&typedef_stmt->method_table, method_ident, fndef);
}
