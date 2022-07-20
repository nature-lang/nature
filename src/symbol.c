#include "symbol.h"
#include "src/semantic/analysis.h"
#include "src/lib/helper.h"

#define BOOL_SIZE_BYTE 1
#define INT_SIZE_BYTE 8
#define FLOAT_SIZE_BYTE 16
#define POINT_SIZE_BYTE 8

void symbol_ident_table_init() {
    symbol_ident_table = table_new();
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

    analysis_local_ident *local = NEW(analysis_local_ident);
    local->unique_ident = unique_ident;
    local->decl = var_decl;
    local->belong = SYMBOL_TYPE_VAR;

    // 添加到符号表中
    table_set(symbol_ident_table, unique_ident, local);
}

bool is_extern_symbol(char *ident) {
    if (str_equal(ident, "print")) {
        return true;
    }
    if (str_equal(ident, "debug_printf")) {
        return true;
    }
    return false;
}

