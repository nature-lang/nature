/**
 * 类型推导与判断
 */
#ifndef NATURE_SRC_AST_INFER_H_
#define NATURE_SRC_AST_INFER_H_

#include "src/ast.h"
#include "src/structs.h"

void infer(module_t *m);


void infer_var_decl(module_t *m, ast_var_decl *var_decl);

static typeuse_t reduction_type(module_t *m, typeuse_t t);

static typeuse_t infer_fndef_decl(module_t *m, ast_fndef_t *fndef);

static void infer_stmt(module_t *m, ast_stmt *stmt);

static typeuse_t infer_right_expr(module_t *m, ast_expr *expr, typeuse_t target_type);

static typeuse_t infer_left_expr(module_t *m, ast_expr *expr);

#endif //NATURE_SRC_AST_INFER_H_
