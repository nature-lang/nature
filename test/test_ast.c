#include "src/ast.h"
#include <stdlib.h>

/**
 * int main() {
 *    int a = 1 + 3
 *    print("php 是世界上最好的语言")
 *    return a
 * }
 */
static void test_ast() {
  // body
  ast_block_stmt body;
  ast_init_block_stmt(&body);

  // int a = 1 + 3
  ast_literal *operandLeft = malloc(sizeof(ast_literal));
  ast_literal *operandRight = malloc(sizeof(ast_literal));
  operandLeft->type = AST_BASE_TYPE_INT;
  operandLeft->value = "1";
  operandRight->type = AST_BASE_TYPE_INT;
  operandRight->value = "3";
  ast_expr tempLeft;
  ast_expr tempRight;
  tempLeft.type = AST_EXPR_TYPE_LITERAL;
  tempLeft.expr = operandLeft;
  tempRight.type = AST_EXPR_TYPE_LITERAL;
  tempRight.expr = operandRight;
  // right
  ast_binary_expr *termExpr = malloc(sizeof(ast_binary_expr));
  termExpr->operator = AST_EXPR_OPERATOR_ADD;
  termExpr->left = tempLeft;
  termExpr->right = tempRight;
  ast_expr tempAdd;
  tempAdd.type = AST_EXPR_TYPE_BINARY;
  tempAdd.expr = termExpr;
  // decl and assign
  ast_var_decl_assign_stmt *decl = malloc(sizeof(ast_var_decl_assign_stmt));
  decl->type = AST_BASE_TYPE_INT;
  decl->ident = "a";
  decl->expr = tempAdd;
//  decl->expr = tempAdd.expr
  ast_stmt stmt;
  stmt.type = "variable_declaration_assign";
  stmt.stmt = decl;
  ast_insert_block_stmt(&body, stmt);

  // print "php 是世界上最好的语言"
  //  CallFunctionExpression
  ast_call_function *call = malloc(sizeof(ast_call_function));
  ast_literal *operandA = malloc(sizeof(ast_literal));
  operandA->type = AST_BASE_TYPE_STRING;
  operandA->value = "php 是世界上最好的语言";
  ast_expr expr;
  expr.type = "literal";
  expr.expr = operandA;
  call->ident = "print";
  call->actual_params[0] = expr;
  ast_stmt tempCall;
  tempCall.type = "call_function";
  tempCall.stmt = call;
  ast_insert_block_stmt(&body, tempCall);

  // return stmt
  ast_return_stmt *ret = malloc(sizeof(ast_return_stmt));
  ast_literal *literalA = malloc(sizeof(ast_literal));
  literalA->type = AST_EXPR_TYPE_LITERAL;
  literalA->value = "a";
  ast_expr tempLiteralA;
  tempLiteralA.type = AST_EXPR_TYPE_LITERAL;
  tempLiteralA.expr = literalA;
  ret->expr = tempLiteralA;
  ast_stmt retTemp;
  retTemp.type = "return";
  retTemp.stmt = ret;
  ast_insert_block_stmt(&body, retTemp);

  // 所有都包含在 main 中
  ast_function_decl *main = malloc(sizeof(ast_function_decl));
  main->name = "main";
  main->return_type = AST_BASE_TYPE_INT;
  main->body = body;
};