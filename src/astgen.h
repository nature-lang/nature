#ifndef NATURE_SRC_ASTGEN_H_
#define NATURE_SRC_ASTGEN_H_

#include "ast.h"

static inline ast_for_tradition_stmt_t *astgen_for_range_rewrite(ast_for_iterator_stmt_t *for_iter) {
    int line = for_iter->range_start.line;
    int column = for_iter->range_start.column;
    if (line <= 0) {
        line = for_iter->range_end.line;
        column = for_iter->range_end.column;
    }

    ast_for_tradition_stmt_t *rewrite = NEW(ast_for_tradition_stmt_t);

    ast_stmt_t *init = NEW(ast_stmt_t);
    init->line = line;
    init->column = column;
    init->assert_type = AST_STMT_VARDEF;
    ast_vardef_stmt_t *init_vardef = NEW(ast_vardef_stmt_t);
    init_vardef->var_decl = for_iter->first;
    init_vardef->right = NEW(ast_expr_t);
    *init_vardef->right = for_iter->range_start;
    init->value = init_vardef;

    ast_expr_t cond = {
            .line = line,
            .column = column,
            .assert_type = AST_EXPR_BINARY,
    };
    ast_binary_expr_t *cond_binary = NEW(ast_binary_expr_t);
    cond_binary->op = AST_OP_LT;
    cond_binary->left = *ast_ident_expr(line, column, for_iter->first.ident);
    cond_binary->right = for_iter->range_end;
    cond.value = cond_binary;

    ast_stmt_t *update = NEW(ast_stmt_t);
    update->line = line;
    update->column = column;
    update->assert_type = AST_STMT_ASSIGN;
    ast_assign_stmt_t *update_assign = NEW(ast_assign_stmt_t);
    update_assign->left = *ast_ident_expr(line, column, for_iter->first.ident);

    ast_expr_t update_right = {
            .line = line,
            .column = column,
            .assert_type = AST_EXPR_BINARY,
    };
    ast_binary_expr_t *update_binary = NEW(ast_binary_expr_t);
    update_binary->op = AST_OP_ADD;
    update_binary->left = *ast_ident_expr(line, column, for_iter->first.ident);
    update_binary->right = *ast_int_expr(line, column, 1);
    update_right.value = update_binary;
    update_assign->right = update_right;
    update->value = update_assign;

    rewrite->init = init;
    rewrite->cond = cond;
    rewrite->update = update;
    rewrite->body = for_iter->body;
    return rewrite;
}

#endif // NATURE_SRC_ASTGEN_H_
