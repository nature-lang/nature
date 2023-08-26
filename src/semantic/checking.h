/**
 * 类型推导与判断
 */
#ifndef NATURE_SRC_AST_CHECKING_H_
#define NATURE_SRC_AST_CHECKING_H_

#include "src/ast.h"
#include "src/types.h"

#define GEN_REWRITE_SEPARATOR "@"

void pre_checking(module_t *m);

void checking(module_t *m);

void checking_var_decl(module_t *m, ast_var_decl_t *var_decl);

static type_t reduction_type(module_t *m, type_t t);

static type_t checking_fn_decl(module_t *m, ast_fndef_t *fndef);

static void checking_stmt(module_t *m, ast_stmt_t *stmt);

static type_t checking_expr(module_t *m, ast_expr_t *expr, type_t target_type);

static type_t checking_right_expr(module_t *m, ast_expr_t *expr, type_t target_type);

static type_t checking_left_expr(module_t *m, ast_expr_t *expr);

#endif //NATURE_SRC_AST_CHECKING_H_
