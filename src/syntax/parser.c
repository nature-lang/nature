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
    [TOKEN_LEFT_PAREN] = {parser_grouping, parser_call_expr, PRECEDENCE_CALL},
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
    return parser_auto_infer_decl();
  } else if (parser_is_base_type() || parser_is_custom_type()) {
    return parser_var_or_function_decl();
  } else if (parser_is(TOKEN_LIST) || parser_is(TOKEN_MAP) || parser_is(TOKEN_FUNCTION)) {

  } else if (parser_is(TOKEN_LITERAL_IDENT)) {
    // foo() {}
    if (parser_next_is(1, TOKEN_LEFT_PAREN)
        && !parser_is_call()) {
      return parser_null_function_decl();
    }

    // judge is call ?
    // foo();
    // foo.bar();
    // foo[1]();
    if (parser_is_call()) {
      return parser_call_stmt();
    }


    // foo = 1
    // foo.bar = 1
    // foo[1] = 1
    // ast_call
    return parser_assign();

  } else if (parser_is(TOKEN_IF)) {

  } else if (parser_is(TOKEN_FOR)) {

  }

  parser_must(TOKEN_STMT_EOF);
}

/**
 * var foo = expr
 * @return
 */
ast_stmt parser_auto_infer_decl() {
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

/**
 * int foo = 12;
 * int foo;
 * int foo() {};
 * @return
 */
ast_stmt parser_var_or_function_decl() {
  ast_stmt result;

  //var_decl
  ast_var_decl *var_decl = parser_var_decl();

  // int foo = 12;
  if (parser_is(TOKEN_EQUAL)) {
    ast_var_decl_assign_stmt *stmt = malloc(sizeof(ast_var_decl_assign_stmt));
    stmt->type = var_decl->type;
    stmt->ident = var_decl->ident;
    stmt->expr = parser_expr(PRECEDENCE_NULL);
    result.type = AST_STMT_VAR_DECL_ASSIGN;
    result.stmt = stmt;
    return result;
  }

  // int foo() {}
  if (parser_is(TOKEN_LEFT_PAREN)) {
    ast_function_decl *function_decl = malloc(sizeof(ast_function_decl));
    function_decl->return_type = var_decl->type;
    function_decl->name = var_decl->ident;

    parser_formal_param(function_decl);
    function_decl->body = parser_block();
    result.type = AST_FUNCTION_DECL;
    result.stmt = function_decl;
    return result;
  }

  // int foo
  ast_var_decl *stmt = malloc(sizeof(ast_var_decl));
  stmt->type = var_decl->type;
  stmt->ident = var_decl->ident;
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

ast_expr parser_call_expr(ast_expr name) {
  ast_expr result;

  ast_call *call_expr = malloc(sizeof(ast_call));
  call_expr->name = name;

  parser_actual_param(call_expr);

  result.type = AST_CALL;
  result.expr = call_expr;

  return result;
}

ast_stmt parser_null_function_decl() {
  ast_stmt result;
  ast_function_decl *function_decl = malloc(sizeof(ast_function_decl));
  token *name_token = parser_must(TOKEN_LITERAL_IDENT);
  function_decl->name = name_token->literal;
  function_decl->return_type = AST_BASE_TYPE_NULL;

  parser_formal_param(function_decl);

  function_decl->body = parser_block();
  result.type = AST_FUNCTION_DECL;
  result.stmt = function_decl;

  return result;
}

ast_stmt parser_call_stmt() {
  ast_stmt result;
  // left_expr
  ast_expr name_expr = parser_expr(PRECEDENCE_NULL);

  ast_call *call_stmt = malloc(sizeof(ast_call));
  call_stmt->name = name_expr;

  // param handle
  parser_actual_param(call_stmt);

  return result;
}

void parser_actual_param(ast_call *call) {
  parser_must(TOKEN_LEFT_PAREN);
  // 参数解析 call(1 + 1, param_a)
  ast_expr first_param = parser_expr(PRECEDENCE_NULL);
  call->actual_params[0] = first_param;
  call->actual_param_count = 1;

  while (parser_is(TOKEN_COMMA)) {
    parser_guard_advance();
    ast_expr rest_param = parser_expr(PRECEDENCE_NULL);
    call->actual_params[call->actual_param_count++] = rest_param;
  }

  parser_must(TOKEN_RIGHT_PAREN);
}

ast_var_decl *parser_var_decl() {
  token *var_type = parser_guard_advance();
  token *var_ident = parser_guard_advance();
  ast_var_decl *var_decl = malloc(sizeof(var_decl));
  var_decl->type = var_type->literal;
  var_decl->ident = var_ident->literal;
  return var_decl;
}

void parser_formal_param(ast_function_decl *function_decl) {
  parser_must(TOKEN_LEFT_PAREN);

  // formal parameter handle type + ident
  function_decl->formal_params[0] = parser_var_decl();
  function_decl->formal_param_count = 1;

  while (parser_is(TOKEN_COMMA)) {
    parser_guard_advance();
    uint8_t count = function_decl->formal_param_count++;
    function_decl->formal_params[count] = parser_var_decl();
  }
  parser_must(TOKEN_RIGHT_PAREN);
}
