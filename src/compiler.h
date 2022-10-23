#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "utils/list.h"
#include "ast.h"
#include "src/lir/lir.h"
#include "utils/list.h"
#include "utils/slice.h"

slice_t *compiler_closures;

slice_t *compiler(ast_closure_t *ast);

list *compiler_closure(closure_t *parent, ast_closure_t *ast_closure, lir_operand_t *target);

list *compiler_block(closure_t *c, slice_t *block);

list *compiler_var_decl(closure_t *c, ast_var_decl *var_decl);

list *compiler_ident(closure_t *c, ast_ident *ident, lir_operand_t *target);

list *compiler_if(closure_t *c, ast_if_stmt *if_stmt);

list *compiler_for_in(closure_t *c, ast_for_in_stmt *ast);

list *compiler_while(closure_t *c, ast_while_stmt *ast);

list *compiler_var_decl_assign(closure_t *c, ast_var_decl_assign_stmt *stmt);

list *compiler_assign(closure_t *c, ast_assign_stmt *stmt);

list *compiler_array_value(closure_t *c, ast_expr expr, lir_operand_t *target);

list *compiler_stmt(closure_t *c, ast_stmt *stmt);

list *compiler_expr(closure_t *c, ast_expr expr, lir_operand_t *target);

list *compiler_call(closure_t *c, ast_call *call, lir_operand_t *target);

list *compiler_builtin_print(closure_t *c, ast_call *call, string print_suffix);

list *compiler_return(closure_t *c, ast_return_stmt *ast);

list *compiler_new_array(closure_t *c, ast_expr expr, lir_operand_t *base_target);

list *compiler_access_map(closure_t *c, ast_expr expr, lir_operand_t *target);

list *compiler_new_map(closure_t *c, ast_expr expr, lir_operand_t *base_target);

list *compiler_access_env(closure_t *c, ast_expr expr, lir_operand_t *target);

list *compiler_new_struct(closure_t *c, ast_expr expr, lir_operand_t *base_target);

list *compiler_select_property(closure_t *c, ast_expr expr, lir_operand_t *target);

list *compiler_literal(closure_t *c, ast_literal *ast, lir_operand_t *target);

/**
 * 二元表达式
 * @param expr
 * @param target
 * @return
 */
list *compiler_binary(closure_t *c, ast_expr expr, lir_operand_t *target);

list *compiler_unary(closure_t *c, ast_expr expr, lir_operand_t *target);

#endif //NATURE_SRC_COMPILER_H_
