/**
 * 类型推导与判断
 */
#ifndef NATURE_SRC_AST_INFER_H_
#define NATURE_SRC_AST_INFER_H_

#include "src/ast.h"

typedef struct infer_closure {
    struct infer_closure *parent;
    ast_closure_t *closure_decl;
} infer_closure;

infer_closure *infer_current;
int infer_line;

infer_closure *infer_current_init(ast_closure_t *closure_decl);

void infer(ast_closure_t *closure_decl);

void infer_block(slice_t *block_stmt);

void infer_stmt(ast_stmt *stmt);

type_t infer_closure_decl(ast_closure_t *closure_decl);

void infer_var_decl(ast_var_decl *var_decl);

void infer_var_decl_assign(ast_var_decl_assign_stmt *stmt);

void infer_assign(ast_assign_stmt *stmt);

void infer_if(ast_if_stmt *stmt);

void infer_while(ast_while_stmt *stmt);

void infer_for_in(ast_for_in_stmt *stmt);

void infer_return(ast_return_stmt *stmt);
//void infer_type_decl(ast_type_decl_stmt *stmt);


type_t infer_expr(ast_expr *expr);

type_t infer_binary(ast_binary_expr *expr);

type_t infer_unary(ast_unary_expr *expr);

type_t infer_ident(string unique_ident);

type_t infer_literal(ast_literal *literal);

type_t infer_new_array(ast_new_list *new_list);

type_t infer_new_map(ast_new_map *new_map);

type_t infer_new_struct(ast_new_struct *new_struct);

type_t infer_access(ast_expr *expr);

type_t infer_access_env(ast_access_env *expr);

type_t infer_select_property(ast_select_property *select_property);

type_t infer_call(ast_call *call);

/**
 * struct 允许顺序不通，但是 key 和 code 需要相同，在还原时需要根据 key 进行排序
 * @param type
 * @return
 */
type_t infer_type(type_t type);

type_t infer_type_decl_ident(ast_ident *ident);

/**
 * @param ident
 * @return
 */
type_t infer_struct_property_type(ast_struct_decl *struct_decl, string ident);

/**
 * if (expr_type.category == TYPE_VAR && stmt->var_decl->code.category == TYPE_VAR) {
 *  error_exit(, "var code ambiguity");
 * }
 * @param left
 * @param right
 * @return
 */
bool infer_compare_type(type_t left, type_t right);

void infer_sort_struct_decl(ast_struct_decl *struct_decl);

bool infer_var_type_can_confirm(type_t right);

#endif //NATURE_SRC_AST_INFER_H_
