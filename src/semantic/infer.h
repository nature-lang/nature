/**
 * 类型推导与判断
 */
#ifndef NATURE_SRC_AST_INFER_H_
#define NATURE_SRC_AST_INFER_H_

#include "src/ast.h"

typedef struct infer_closure {
  struct infer_closure *parent;
  ast_closure_decl *closure_decl;
} infer_closure;

infer_closure *infer_current;

infer_closure *infer_current_init(ast_closure_decl *closure_decl);
void infer(ast_closure_decl *closure_decl);

void infer_block(ast_block_stmt *block_stmt);
void infer_stmt(ast_stmt *stmt);
ast_type infer_closure_decl(ast_closure_decl *closure_decl);
void infer_var_decl(ast_var_decl *var_decl);
void infer_var_decl_assign(ast_var_decl_assign_stmt *stmt);
void infer_assign(ast_assign_stmt *stmt);
void infer_if(ast_if_stmt *stmt);
void infer_while(ast_while_stmt *stmt);
void infer_for_in(ast_for_in_stmt *stmt);
void infer_return(ast_return_stmt *stmt);
//void infer_type_decl(ast_type_decl_stmt *stmt);


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

/**
 * struct 允许顺序不通，但是 key 和 type 需要相同，在还原时需要根据 key 进行排序
 * @param type
 * @return
 */
ast_type infer_type(ast_type type);

ast_type infer_type_decl_ident(string ident);

/**
 * @param ident
 * @return
 */
ast_type infer_struct_property_type(ast_struct_decl *struct_decl, string ident);

/**
 * if (expr_type.category == TYPE_VAR && stmt->var_decl->type.category == TYPE_VAR) {
 *  error_exit(0, "var type ambiguity");
 * }
 * @param left
 * @param right
 * @return
 */
bool infer_compare_type(ast_type left, ast_type right);

void infer_sort_struct_decl(ast_struct_decl *struct_decl);

#endif //NATURE_SRC_AST_INFER_H_
