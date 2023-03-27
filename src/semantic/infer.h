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

typedecl_t infer_closure_decl(ast_closure_t *closure_decl);

void infer_var_decl(ast_var_decl *var_decl);

void infer_var_decl_assign(ast_var_decl_assign_stmt *stmt);

void infer_assign(ast_assign_stmt *stmt);

void infer_if(ast_if_stmt *stmt);

void infer_while(ast_while_stmt *stmt);

void infer_for_in(ast_for_in_stmt *stmt);

void infer_return(ast_return_stmt *stmt);
//void infer_type_decl(ast_type_decl_stmt *stmt);

typedecl_t infer_expr(ast_expr *expr);

typedecl_t infer_binary(ast_binary_expr *expr);

typedecl_t infer_unary(ast_unary_expr *expr);

typedecl_t infer_ident(string unique_ident);

typedecl_t infer_literal(ast_literal *literal);

typedecl_t infer_new_list(ast_new_list *new_list);

typedecl_t infer_new_map(ast_map_new *new_map);

typedecl_t infer_new_struct(ast_struct_new_t *new_struct);

typedecl_t infer_access(ast_expr *expr);

typedecl_t infer_access_env(ast_env_value *expr);

typedecl_t infer_struct_access(ast_struct_access *struct_access);

typedecl_t infer_call(ast_call *call);

/**
 * struct 允许顺序不通，但是 key 和 code 需要相同，在还原时需要根据 key 进行排序
 * @param type
 * @return
 */
typedecl_t infer_type(typedecl_t type);

typedecl_t infer_type_def(typedecl_ident_t *def);

/**
 * @param ident
 * @return
 */
typedecl_t infer_struct_property_type(typedecl_struct_t *struct_decl, string ident);

//void infer_sort_struct_decl(typedecl_struct_t *struct_decl);

bool infer_var_type_can_confirm(typedecl_t right);

#endif //NATURE_SRC_AST_INFER_H_
