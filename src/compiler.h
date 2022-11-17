#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "utils/list.h"
#include "ast.h"
#include "src/lir/lir.h"
#include "utils/list.h"
#include "utils/slice.h"

slice_t *compiler_closures;
module_t *current_module;

slice_t *compiler(module_t *m, ast_closure_t *ast);

list *compiler_closure(closure_t *parent, ast_closure_t *ast_closure, lir_operand_t *target);

list *compiler_block(closure_t *c, slice_t *block);

list *compiler_var_decl(closure_t *c, ast_var_decl *var_decl);

list *compiler_if(closure_t *c, ast_if_stmt *if_stmt);

list *compiler_for_in(closure_t *c, ast_for_in_stmt *ast);

list *compiler_while(closure_t *c, ast_while_stmt *ast);

list *compiler_var_decl_assign(closure_t *c, ast_var_decl_assign_stmt *stmt);

list *compiler_assign(closure_t *c, ast_assign_stmt *stmt);

list *compiler_stmt(closure_t *c, ast_stmt *stmt);

lir_operand_t *compiler_expr(closure_t *c, list *operations, ast_expr expr);

/**
 * 二元表达式
 * @param expr
 * @param target
 * @return
 */
lir_operand_t *compiler_binary(closure_t *c, list *operations, ast_expr expr);

lir_operand_t *compiler_unary(closure_t *c, list *operations, ast_expr expr);

lir_operand_t *compiler_call(closure_t *c, list *operations, ast_expr expr);

lir_operand_t *compiler_new_array(closure_t *c, list *operations, ast_expr expr);

lir_operand_t *compiler_array_value(closure_t *c, list *operations, ast_expr expr);

lir_operand_t *compiler_env_value(closure_t *c, list *operations, ast_expr expr);

lir_operand_t *compiler_map_value(closure_t *c, list *operations, ast_expr expr);

lir_operand_t *compiler_new_map(closure_t *c, list *operations, ast_expr expr);

lir_operand_t *compiler_select_property(closure_t *c, list *operations, ast_expr expr);

list *compiler_return(closure_t *c, ast_return_stmt *ast);

#endif //NATURE_SRC_COMPILER_H_
