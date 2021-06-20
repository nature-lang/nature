#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "src/lib/list.h"
#include "src/ast/ast.h"
#include "src/lir.h"

// 入口
list_op *compiler(closure *c, ast_closure_decl *decl);

list_op *compiler_block(closure *c, ast_block_stmt *block);

list_op *compiler_var_decl(closure *c, ast_var_decl_stmt *var_decl);

list_op *compiler_if(closure *c, ast_if_stmt *if_stmt);

list_op *compiler_var_decl_assign(closure *c, ast_var_decl_assign_stmt *stmt);

list_op *compiler_assign(closure *c, ast_assign_stmt *stmt);

list_op *compiler_access_list(closure *c, ast_access_list *ast, lir_operand *target);

list_op *compiler_expr(closure *c, ast_expr expr, lir_operand *target);

list_op *compiler_call(closure *c, ast_call *call, lir_operand *target);

list_op *compiler_new_list(closure *c, ast_new_list *new_list, lir_operand *target);

/**
 * 二元表达式
 * @param expr
 * @param target
 * @return
 */
list_op *compiler_binary(closure *c, ast_binary_expr *expr, lir_operand *target);

#endif //NATURE_SRC_COMPILER_H_
