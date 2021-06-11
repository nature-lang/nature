#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "src/lib/list.h"
#include "src/ast/ast.h"
#include "src/lir.h"

// 入口
list_op *compiler(ast_closure_decl *decl);

list_op *compiler_block(ast_block_stmt *block);

list_op *compiler_var_decl(ast_var_decl_stmt *var_decl);

list_op *compiler_var_decl_assign(ast_var_decl_assign_stmt *stmt);

list_op *compiler_expr(ast_expr expr, lir_operand target);

/**
 * 二元表达式
 * @param expr
 * @param target
 * @return
 */
list_op *compiler_binary(ast_binary_expr *expr, lir_operand target);

#endif //NATURE_SRC_COMPILER_H_
