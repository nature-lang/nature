#include "compiler.h"

list_op *compiler_block(ast_block_stmt *block) {
  list_op *operates = list_op_new();
  for (int i = 0; i < block->count; ++i) {
    ast_stmt stmt = block->list[i];
    list_op *await_append;

    switch (stmt.type) {
      case AST_STMT_VAR_DECL: {
        await_append = compiler_var_decl((ast_var_decl_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_VAR_DECL_ASSIGN: {
        await_append = compiler_var_decl_assign((ast_var_decl_assign_stmt *) stmt.stmt);
        break;
      }
    }

    list_op_append(operates, await_append);
  }

  return operates;
}

/**
 * a = b + 1 + 3
 * @param stmt
 * @return
 */
list_op *compiler_var_decl_assign(ast_var_decl_assign_stmt *stmt) {
  lir_operand target = lir_new_var_operand(stmt->ident);

  // first and

  return NULL;
}

list_op *compiler_var_decl(ast_var_decl_stmt *var_decl) {
  return NULL;
}

list_op *compiler_expr(ast_expr expr, lir_operand target) {
  switch (expr.type) {
    case AST_EXPR_TYPE_BINARY: {
      return compiler_binary((ast_binary_expr *) expr.expr, target);
    }
    case AST_EXPR_TYPE_LITERAL: { // 1,1.1,true
      break;
    }
    case AST_EXPR_TYPE_IDENT: {
      break;
    }
  }
  return NULL;
}

list_op *compiler_binary(ast_binary_expr *expr, lir_operand result_target) {
  uint8_t type;
  switch (expr->operator) {
    case AST_EXPR_OPERATOR_ADD: {
      type = LIR_OP_TYPE_ADD;
      break;
    }
  }

  lir_operand left_target = lir_new_temp_var_operand();
  lir_operand right_target = lir_new_temp_var_operand();
  list_op *operates = compiler_expr(expr->left, left_target);
  list_op_append(operates, compiler_expr(expr->right, right_target));
  lir_op *binary_op = lir_new_op();
  binary_op->type = type;
  binary_op->result = result_target;
  binary_op->first = left_target;
  binary_op->second = right_target;
  list_op_push(operates, binary_op);

  return operates;
}


