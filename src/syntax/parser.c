#include "parser.h"

parser_cursor p_cursor;

ast_block_stmt parser(list *token_list) {
  ast_block_stmt block_stmt = ast_new_block_stmt();
  // 读取一段 token
  while (!parser_is(TOKEN_EOF)) {
    ast_block_stmt_push(&block_stmt, parser_stmt());
  }

  return block_stmt;
}

ast_block_stmt parser_block() {
  ast_block_stmt block_stmt = ast_new_block_stmt();

  parser_must(TOKEN_LEFT_CURLY); // 必须是
  while (!parser_is(TOKEN_RIGHT_CURLY)) {
    ast_block_stmt_push(&block_stmt, parser_stmt());
  }
  parser_must(TOKEN_RIGHT_CURLY);

  return block_stmt;
}

ast_stmt parser_stmt() {
  if (parser_is(TOKEN_VAR)) { // var 必须定义的同时存在右值。
    return parser_auto_var_decl();
  } else if (parser_is(TOKEN_IN)
      || parser_is(TOKEN_BOOL)
      || parser_is(TOKEN_FLOAT)
      || parser_is(TOKEN_STRING)) {
    return parser_type_var_decal();
  } else if (parser_is(TOKEN_LIST) || parser_is(TOKEN_MAP) || parser_is(TOKEN_FUNCTION)) {

  }

  parser_must(TOKEN_STMT_EOF);
}

ast_stmt parser_auto_var_decl() {
  ast_stmt result;
  ast_var_decl_assign_stmt *stmt = malloc(sizeof(ast_var_decl_assign_stmt));

  parser_must(TOKEN_VAR);
  // 变量名称
  token *t = parser_must(TOKEN_LITERAL_IDENT);
  stmt->type = "var";
  stmt->ident = t->literal;
  stmt->expr = parser_expr();

  result.type = AST_STMT_VAR_DECL_ASSIGN;
  result.stmt = stmt;

  return result;
}

/**
 * 表达式优先级处理方式
 * @return
 */
ast_expr parser_expr() {
  ast_expr result;
  return result;
}

ast_stmt parser_type_var_decal() {
  ast_stmt result;

  //ast_var_decl
  //ast_var_decl_assign_stmt
  // 各种类型如何处理, 直接读取可控类型
  token *var_type = parser_guard_advance();
  token *var_ident = parser_must(TOKEN_LITERAL_IDENT);
  if (parser_is(TOKEN_EQUAL)) {
    ast_var_decl_assign_stmt *stmt = malloc(sizeof(ast_var_decl_assign_stmt));
    stmt->type = var_type->literal;
    stmt->ident = var_ident->literal;
    stmt->expr = parser_expr();
    result.type = AST_STMT_VAR_DECL_ASSIGN;
    result.stmt = stmt;
    return result;
  }

  ast_var_decl *stmt = malloc(sizeof(ast_var_decl));
  stmt->type = var_type->literal;
  stmt->ident = var_ident->literal;
  result.type = AST_VAR_DECL;
  result.stmt = stmt;

  return result;
}
