#include "parser.h"

parser_cursor p_cursor;

int8_t token_to_ast_expr_operator[] = {
    [TOKEN_PLUS] = AST_EXPR_OPERATOR_ADD,
    [TOKEN_MINUS] = AST_EXPR_OPERATOR_SUB,
    [TOKEN_STAR] = AST_EXPR_OPERATOR_MUL,
    [TOKEN_SLASH] = AST_EXPR_OPERATOR_DIV,
};

int8_t token_to_ast_base_type[] = {
    [TOKEN_BOOL] = AST_BASE_TYPE_BOOL,
    [TOKEN_FLOAT] = AST_BASE_TYPE_FLOAT,
    [TOKEN_INT] = AST_BASE_TYPE_INT,
    [TOKEN_STRING] = AST_BASE_TYPE_STRING
};

parser_rule rules[] = {
    [TOKEN_LEFT_PAREN] = {parser_grouping, parser_call, PRECEDENCE_CALL},
    // map["foo"] list[0]
    [TOKEN_LEFT_SQUARE] = {NULL, parser_access, PRECEDENCE_CALL},
    // struct.property
    [TOKEN_DOT] = {NULL, parser_select, PRECEDENCE_CALL},
    [TOKEN_MINUS] = {parser_unary, parser_binary, PRECEDENCE_TERM},
    [TOKEN_PLUS] = {parser_unary, parser_binary, PRECEDENCE_TERM},
    [TOKEN_SLASH] = {NULL, parser_binary, PRECEDENCE_FACTOR},
    [TOKEN_STAR] = {NULL, parser_binary, PRECEDENCE_FACTOR},
    [TOKEN_NOT_EQUAL] = {NULL, parser_binary, PRECEDENCE_EQUALITY},
    [TOKEN_EQUAL_EQUAL] = {NULL, parser_binary, PRECEDENCE_EQUALITY},
    [TOKEN_GREATER] = {NULL, parser_binary, PRECEDENCE_COMPARE},
    [TOKEN_GREATER_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
    [TOKEN_LESS] = {NULL, parser_binary, PRECEDENCE_COMPARE},
    [TOKEN_LESS_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
    [TOKEN_LITERAL_IDENT] = {parser_var, NULL, PRECEDENCE_NULL},
    [TOKEN_LITERAL_STRING] = {parser_literal, NULL, PRECEDENCE_NULL},
    [TOKEN_LITERAL_INT] = {parser_literal, NULL, PRECEDENCE_NULL},
    [TOKEN_LITERAL_FLOAT] = {parser_literal, NULL, PRECEDENCE_NULL},
    [TOKEN_TRUE] = {parser_literal, NULL, PRECEDENCE_NULL},
    [TOKEN_FALSE] = {parser_literal, NULL, PRECEDENCE_NULL},
};

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
ast_expr parser_expr(parser_precedence precedence) {
  // 读取表达式前缀
  parser_prefix_fn prefix_fn = parser_get_rule(parser_guard_peek()->type)->prefix;
  if (prefix_fn == NULL) {
    // TODO error exit
  }

  ast_expr prefix_ast_expr = prefix_fn(); // advance

  // 前缀表达式已经处理完成，判断是否有中缀表达式，有则处理
  parser_rule *infix_rule = parser_get_rule(parser_guard_peek()->type);
  // 大于表示优先级较高，需要先处理其余表达式 (怎么处理)
  if (infix_rule != NULL && infix_rule->infix_precedence >= precedence) {
    parser_infix_fn infix_fn = infix_rule->infix;
    return infix_fn(prefix_ast_expr);
  }

  return prefix_ast_expr;
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
    stmt->expr = parser_expr(PRECEDENCE_NULL);
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

ast_expr parser_binary(ast_expr left) {
  ast_expr result;

  token *operator_token = parser_guard_advance();

  // right expr
  parser_precedence precedence = parser_get_rule(operator_token->type)->infix_precedence;
  ast_expr right = parser_expr(precedence + 1);

  ast_binary_expr *binary_expr = malloc(sizeof(ast_binary_expr));

  binary_expr->operator = token_to_ast_expr_operator[operator_token->type];
  binary_expr->left = left;
  binary_expr->right = right;

  return result;
}

ast_expr parser_unary() {
  ast_expr result;
  token *operator_token = parser_guard_advance();
  ast_expr operand = parser_expr(PRECEDENCE_UNARY);

  ast_unary_expr *unary_expr = malloc(sizeof(ast_unary_expr));
  unary_expr->operator = token_to_ast_expr_operator[operator_token->type];
  unary_expr->operand = operand;

  return result;
}

/**
 * 仅仅产生优先级，啥都不做哦, 直接 return 就好
 */
ast_expr parser_grouping() {
  parser_must(TOKEN_LEFT_PAREN);
  ast_expr expr = parser_expr(PRECEDENCE_NULL);
  parser_must(TOKEN_RIGHT_PAREN);
  return expr;
}

ast_expr parser_literal() {
  ast_expr result;
  token *literal_token = parser_guard_advance();
  ast_literal *literal_expr = malloc(sizeof(ast_literal));
  literal_expr->type = token_to_ast_base_type[literal_token->type];
  literal_expr->value = literal_token->literal;

  result.type = AST_EXPR_TYPE_LITERAL;
  result.expr = literal_expr;

  return result;
}

ast_expr parser_var() {
  ast_expr result;
  token *ident_token = parser_guard_advance();
  ast_ident *ident = ident_token->literal;

  result.type = AST_EXPR_TYPE_IDENT;
  result.expr = ident;

  return result;
}

/**
 * foo[expr]
 * @param left
 * @return
 */
ast_expr parser_access(ast_expr left) {
  ast_expr result;

  parser_must(TOKEN_LEFT_SQUARE);
  ast_expr key = parser_expr(PRECEDENCE_CALL); // ??
  parser_must(TOKEN_RIGHT_PAREN);
  ast_access *access_expr = malloc(sizeof(access_expr));
  access_expr->left = left;
  access_expr->key = key;
  result.type = AST_EXPR_TYPE_ACCESS;

  return result;
}

/**
 * foo.bar[car]
 * for.bar.car
 * @param left
 * @return
 */
ast_expr parser_select(ast_expr left) {
  ast_expr result;
  parser_must(TOKEN_DOT);

  token *property_token = parser_must(TOKEN_LITERAL_IDENT);
  ast_select_property *select_property_expr = malloc(sizeof(ast_select_property));
  select_property_expr->left = left;
  select_property_expr->property = property_token->literal;

  return result;
}
