#include "symbol.h"
#include "src/semantic/analysis.h"
#include "src/lib/helper.h"

#define BOOL_SIZE_BYTE 1
#define INT_SIZE_BYTE 8
#define FLOAT_SIZE_BYTE 16
#define POINT_SIZE_BYTE 8

void symbol_ident_table_init() {
    symbol_table = table_new();

    symbol_table_set("print", SYMBOL_TYPE_FN, NULL, false);
    symbol_table_set("debug_printf", SYMBOL_TYPE_FN, NULL, false);
}

// TODO 临时测试使用
bool is_debug_symbol(char *ident) {
    if (str_equal(ident, "print")) {
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
    if (str_equal(ident, "builtin_print")) {
        return true;
    }
    return false;
}

size_t type_sizeof(ast_type type) {
    switch (type.category) {
        case TYPE_BOOL:
            return BOOL_SIZE_BYTE;
        case TYPE_INT:
            return INT_SIZE_BYTE;
        case TYPE_FLOAT:
            return FLOAT_SIZE_BYTE;
        default:
            return POINT_SIZE_BYTE;
    }
}

/**
 * 默认 struct_decl 已经排序过了
 * @param struct_decl
 * @param property
 * @return
 */
size_t struct_offset(ast_struct_decl *struct_decl, char *property) {
    size_t offset = 0;
    for (int i = 0; i < struct_decl->count; ++i) {
        offset += type_sizeof(struct_decl->list[i].type);
    }
    return offset;
}

// compiler 阶段临时生成的数据
void symbol_set_temp_ident(char *unique_ident, ast_type type) {
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
    s->decl = decl;
    s->is_local = is_local;
    table_set(symbol_table, ident, s);
}

symbol_t *symbol_table_get(char *ident) {
    return table_get(symbol_table, ident);
}


