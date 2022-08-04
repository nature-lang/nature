#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "utils/list.h"
#include "ast.h"
#include "src/lir/lir.h"
#include "utils/list.h"

typedef struct {
    size_t count;
    closure *list[FIXED_ARRAY_COUNT];
} compiler_closures;

compiler_closures compiler(ast_closure_decl *ast);

list *compiler_closure(closure *parent, ast_closure_decl *ast_closure, lir_operand *target);

list *compiler_block(closure *c, ast_block_stmt *block);

list *compiler_var_decl(closure *c, ast_var_decl *var_decl);

list *compiler_ident(closure *c, ast_ident *ident, lir_operand *target);

list *compiler_if(closure *c, ast_if_stmt *if_stmt);

list *compiler_for_in(closure *c, ast_for_in_stmt *ast);

list *compiler_while(closure *c, ast_while_stmt *ast);

list *compiler_var_decl_assign(closure *c, ast_var_decl_assign_stmt *stmt);

list *compiler_assign(closure *c, ast_assign_stmt *stmt);

list *compiler_array_value(closure *c, ast_expr expr, lir_operand *target);

list *compiler_stmt(closure *c, ast_stmt stmt);

list *compiler_expr(closure *c, ast_expr expr, lir_operand *target);

list *compiler_call(closure *c, ast_call *call, lir_operand *target);

list *compiler_builtin_print(closure *c, ast_call *call, string print_suffix);

list *compiler_return(closure *c, ast_return_stmt *ast);

list *compiler_new_array(closure *c, ast_expr expr, lir_operand *base_target);

list *compiler_access_map(closure *c, ast_expr expr, lir_operand *target);

list *compiler_new_map(closure *c, ast_expr expr, lir_operand *base_target);

list *compiler_access_env(closure *c, ast_expr expr, lir_operand *target);

list *compiler_new_struct(closure *c, ast_expr expr, lir_operand *base_target);

list *compiler_select_property(closure *c, ast_expr expr, lir_operand *target);

list *compiler_literal(closure *c, ast_literal *ast, lir_operand *target);

/**
 * 二元表达式
 * @param expr
 * @param target
 * @return
 */
list *compiler_binary(closure *c, ast_expr expr, lir_operand *target);

list *compiler_unary(closure *c, ast_expr expr, lir_operand *target);

#endif //NATURE_SRC_COMPILER_H_
