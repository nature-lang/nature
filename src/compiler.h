#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "utils/list.h"
#include "utils/slice.h"
#include "ast.h"
#include "src/lir/lir.h"
#include "utils/list.h"
#include "src/module.h"

typedef struct {
    size_t count;
    closure *list[FIXED_ARRAY_COUNT];
} compiler_closures;

void compiler(module_t *m,ast_closure *ast);

list *compiler_closure(module_t *m, closure *parent, ast_closure *ast_closure, lir_operand *target);

list *compiler_block(module_t *m, closure *c, slice_t *block);

list *compiler_var_decl(module_t *m, closure *c, ast_var_decl *var_decl);

list *compiler_ident(module_t *m, closure *c, ast_ident *ident, lir_operand *target);

list *compiler_if(module_t *m, closure *c, ast_if_stmt *if_stmt);

list *compiler_for_in(module_t *m, closure *c, ast_for_in_stmt *ast);

list *compiler_while(module_t *m, closure *c, ast_while_stmt *ast);

list *compiler_var_decl_assign(module_t *m, closure *c, ast_var_decl_assign_stmt *stmt);

list *compiler_assign(module_t *m, closure *c, ast_assign_stmt *stmt);

list *compiler_array_value(module_t *m, closure *c, ast_expr expr, lir_operand *target);

list *compiler_stmt(module_t *m, closure *c, ast_stmt* stmt);

list *compiler_expr(module_t *m, closure *c, ast_expr expr, lir_operand *target);

list *compiler_call(module_t *m, closure *c, ast_call *call, lir_operand *target);

list *compiler_builtin_print(module_t *m, closure *c, ast_call *call, string print_suffix);

list *compiler_return(module_t *m, closure *c, ast_return_stmt *ast);

list *compiler_new_array(module_t *m, closure *c, ast_expr expr, lir_operand *base_target);

list *compiler_access_map(module_t *m, closure *c, ast_expr expr, lir_operand *target);

list *compiler_new_map(module_t *m, closure *c, ast_expr expr, lir_operand *base_target);

list *compiler_access_env(module_t *m, closure *c, ast_expr expr, lir_operand *target);

list *compiler_new_struct(module_t *m, closure *c, ast_expr expr, lir_operand *base_target);

list *compiler_select_property(module_t *m, closure *c, ast_expr expr, lir_operand *target);

list *compiler_literal(module_t *m, closure *c, ast_literal *ast, lir_operand *target);

/**
 * 二元表达式
 * @param expr
 * @param target
 * @return
 */
list *compiler_binary(module_t *m, closure *c, ast_expr expr, lir_operand *target);

list *compiler_unary(module_t *m, closure *c, ast_expr expr, lir_operand *target);

#endif //NATURE_SRC_COMPILER_H_
