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
      case AST_STMT_ASSIGN: {
        await_append = compiler_assign((ast_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_IF: {
        await_append = compiler_if((ast_if_stmt *) stmt.stmt);
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
  lir_operand *target = lir_new_var_operand(stmt->ident);
  return compiler_expr(stmt->expr, target);
}
list_op *compiler_assign(ast_assign_stmt *stmt) {
  // left 可能是一个数组调用，可能是一个对象访问，也有可能是一个标量赋值
  lir_operand *left_target = lir_new_temp_var_operand();
  lir_operand *right_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(stmt->left, left_target);
  list_op_append(list, compiler_expr(stmt->right, right_target));

  lir_op *move_op = lir_new_op();
  move_op->type = LIR_OP_TYPE_MOVE;
  move_op->result = *left_target;
  move_op->first = *right_target;
  list_op_push(list, move_op);

  return list;
}

list_op *compiler_var_decl(ast_var_decl_stmt *var_decl) {
  return NULL;
}

list_op *compiler_expr(ast_expr expr, lir_operand *target) {
  switch (expr.type) {
    case AST_EXPR_TYPE_BINARY: {
      return compiler_binary((ast_binary_expr *) expr.expr, target);
    }
    case AST_EXPR_TYPE_LITERAL: {
      ast_literal *literal = (ast_literal *) expr.expr;

      if (literal->type == AST_BASE_TYPE_INT || literal->type == AST_BASE_TYPE_FLOAT) {
        lir_operand_immediate *immediate = malloc(sizeof(lir_operand_immediate));
        immediate->type = literal->type;
        immediate->value = literal->value;
        target->type = LIR_OPERAND_TYPE_IMMEDIATE;
        target->value = immediate;
      }

      // TODO 其他类型处理

      break;
    }
    case AST_EXPR_TYPE_IDENT: {
      target->type = LIR_OPERAND_TYPE_VAR;
      target->value = lir_new_var_operand((ast_ident) expr.expr);
      break;
    }
  }
  return NULL;
}

list_op *compiler_binary(ast_binary_expr *expr, lir_operand *result_target) {
  uint8_t type;
  switch (expr->operator) {
    case AST_EXPR_OPERATOR_ADD: {
      type = LIR_OP_TYPE_ADD;
      break;
    }
  }

  lir_operand *left_target = lir_new_temp_var_operand();
  lir_operand *right_target = lir_new_temp_var_operand();
  list_op *operates = compiler_expr(expr->left, left_target);
  list_op_append(operates, compiler_expr(expr->right, right_target));
  lir_op *binary_op = lir_new_op();
  binary_op->type = type;
  binary_op->result = *result_target;
  binary_op->first = *left_target;
  binary_op->second = *right_target;
  list_op_push(operates, binary_op);

  return operates;
}

list_op *compiler_if(ast_if_stmt *if_stmt) {
  // 编译 condition
  lir_operand *condition_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(if_stmt->condition, condition_target);
  // 判断结果是否为 false, false 对应 else
  lir_op *cmp_goto = lir_new_op();
  cmp_goto->type = LIR_OP_TYPE_CMP_GOTO;

  lir_operand_immediate *falsely = malloc(sizeof(lir_operand_immediate));
  falsely->type = AST_BASE_TYPE_BOOL;
  falsely->value = 0;
  lir_operand immediate_operand = {.value = falsely, .type = LIR_OPERAND_TYPE_IMMEDIATE};
  cmp_goto->first = immediate_operand;
  cmp_goto->second = *condition_target;

  lir_op *end_label = lir_new_label("end_if");
  lir_op *alternate_label = lir_new_label("alternate_if");
  if (if_stmt->alternate.count == 0) {
    cmp_goto->result = end_label->result;
  } else {
    cmp_goto->result = alternate_label->result;
  }

  // 编译 consequent block
  list_op *consequent_list = compiler_block(&if_stmt->consequent);
  list_op_push(consequent_list, lir_new_goto(&end_label->result));
  list_op_append(list, consequent_list);

  // 编译 alternate block
  if (if_stmt->alternate.count != 0) {
    list_op_push(list, alternate_label);
    list_op *alternate_list = compiler_block(&if_stmt->alternate);
    list_op_append(list, alternate_list);
  }
  // 追加 end_if 标签
  list_op_push(list, end_label);

  return list;
}



