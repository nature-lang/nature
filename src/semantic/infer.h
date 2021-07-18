/**
 * 类型推导
 */

#ifndef NATURE_SRC_AST_INFER_H_
#define NATURE_SRC_AST_INFER_H_

#include "src/ast.h"

void infer(ast_closure_decl closure_decl);

ast_type infer_closure_decl(ast_closure_decl *closure_decl);

void infer_block(ast_block_stmt *block_stmt);

/**
 * var a = 1
 * var b = 2.0
 * var c = true
 * var d = void (int a, int b) {}
 * var e = [1, 2, 3] // ?
 * var f = {"a": 1, "b": 2} // ?
 * var h = call();
 */
void infer_var_decl_assign(ast_var_decl_assign_stmt);
void infer_var_decl(ast_var_decl *var_decl);
void infer_type(ast_type *type);

ast_type infer_expr(ast_expr *expr);

ast_type infer_binary(ast_binary_expr *expr);
ast_type infer_unary(ast_unary_expr *expr);
ast_type infer_ident(ast_ident *expr);
ast_type infer_new_list(ast_new_list *new_list);
ast_type infer_new_map(ast_new_map *new_map);
ast_type infer_new_struct(ast_new_struct *new_struct);
ast_type infer_access(ast_expr *expr);
ast_type infer_select_property(ast_select_property *select_property);
ast_type infer_call(ast_call *call);
ast_type infer_struct_property_type(string ident);

#endif //NATURE_SRC_AST_INFER_H_
