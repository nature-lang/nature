#include "ast.h"
#include "utils/helper.h"

string ast_expr_operator_to_string[100] = {
        [AST_EXPR_OPERATOR_ADD] = "+",
        [AST_EXPR_OPERATOR_SUB] = "-",
        [AST_EXPR_OPERATOR_MUL] = "*",
        [AST_EXPR_OPERATOR_DIV] = "/",

        [AST_EXPR_OPERATOR_LT] = "<",
        [AST_EXPR_OPERATOR_LTE] = "<=",
        [AST_EXPR_OPERATOR_GT] = ">", // >
        [AST_EXPR_OPERATOR_GTE] = ">=",  // >=
        [AST_EXPR_OPERATOR_EQ_EQ] = "==", // ==
        [AST_EXPR_OPERATOR_NOT_EQ] = "!=", // !=

        [AST_EXPR_OPERATOR_NOT] = "!", // unary !expr
        [AST_EXPR_OPERATOR_NEG] = "-", // unary -expr
};

ast_block_stmt ast_new_block_stmt() {
    ast_block_stmt result;
    result.count = 0;
    result.capacity = 0;
    result.list = NULL;
    return result;
}

void ast_block_stmt_push(ast_block_stmt *block, ast_stmt stmt) {
    if (block->capacity <= block->count) {
        block->capacity = GROW_CAPACITY(block->capacity);
        block->list = (ast_stmt *) realloc(block->list, sizeof(stmt) * block->capacity);
    }

    block->list[block->count++] = stmt;
}

ast_ident *ast_new_ident(char *literal) {
    ast_ident *ident = malloc(sizeof(ast_ident));
    ident->literal = literal;
    return ident;
}

int ast_struct_decl_size(ast_struct_decl *struct_decl) {
    int size = 0;
    for (int i = 0; i < struct_decl->count; ++i) {
        ast_struct_property property = struct_decl->list[i];
        size += type_base_sizeof(property.type.base);
    }
    return size;
}

/**
 * 默认 struct_decl 已经排序过了
 * @param struct_decl
 * @param property
 * @return
 */
int ast_struct_offset(ast_struct_decl *struct_decl, char *property) {
    int offset = 0;
    for (int i = 0; i < struct_decl->count; ++i) {
        ast_struct_property item = struct_decl->list[i];
        if (str_equal(item.key, property)) {
            break;
        }
        offset += type_base_sizeof(item.type.base);
    }
    return offset;
}
