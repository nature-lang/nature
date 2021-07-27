#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "src/lib/list.h"
#include "ast.h"
#include "src/lir.h"

int compiler_line;

// 入口
list_op *compiler(ast_closure_decl *ast);

list_op *compiler_closure(closure *parent, ast_closure_decl *ast);

list_op *compiler_block(closure *c, ast_block_stmt *block);

list_op *compiler_var_decl(closure *c, ast_var_decl *var_decl);

list_op *compiler_if(closure *c, ast_if_stmt *if_stmt);

list_op *compiler_for_in(closure *c, ast_for_in_stmt *ast);

list_op *compiler_while(closure *c, ast_while_stmt *ast);

list_op *compiler_var_decl_assign(closure *c, ast_var_decl_assign_stmt *stmt);

list_op *compiler_assign(closure *c, ast_assign_stmt *stmt);

list_op *compiler_access_list(closure *c, ast_access_list *ast, lir_operand *refer_target);

list_op *compiler_stmt(closure *c, ast_stmt stmt);

list_op *compiler_expr(closure *c, ast_expr expr, lir_operand *target);

list_op *compiler_call(closure *c, ast_call *call, lir_operand *target);

list_op *compiler_return(closure *c, ast_return_stmt *ast);

list_op *compiler_new_list(closure *c, ast_new_list *new_list, lir_operand *target);

list_op *compiler_access_map(closure *c, ast_access_map *ast, lir_operand *target);

list_op *compiler_new_map(closure *c, ast_new_map *ast, lir_operand *base_target);

list_op *compiler_access_env(closure *c, ast_access_env *ast, lir_operand *target);

list_op *compiler_new_struct(closure *c, ast_new_struct *ast, lir_operand *target);

list_op *compiler_select_property(closure *c, ast_select_property *ast, lir_operand *target);

/**
 * 二元表达式
 * @param expr
 * @param target
 * @return
 */
list_op *compiler_binary(closure *c, ast_binary_expr *expr, lir_operand *target);

list_op *compiler_unary(closure *c, ast_unary_expr *expr, lir_operand *target);

#endif //NATURE_SRC_COMPILER_H_
