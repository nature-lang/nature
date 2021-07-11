#ifndef NATURE_SRC_SYNTAX_PARSER_H_
#define NATURE_SRC_SYNTAX_PARSER_H_

#include "src/lib/list.h"
#include "src/ast/ast.h"
#include "token.h"
#include <stdlib.h>

typedef enum {
  PRECEDENCE_NULL, // 最低优先级
  PRECEDENCE_OR,
  PRECEDENCE_AND,
  PRECEDENCE_EQUALITY,
  PRECEDENCE_COMPARE,
  PRECEDENCE_TERM,
  PRECEDENCE_FACTOR,
  PRECEDENCE_UNARY,
  PRECEDENCE_CALL, // foo.bar foo["bar"] foo()
  PRECEDENCE_PRIMARY,
} parser_precedence;

typedef ast_expr (*parser_prefix_fn)();
typedef ast_expr (*parser_infix_fn)(ast_expr prefix);

typedef struct {
  parser_prefix_fn prefix;
  parser_infix_fn infix;
  parser_precedence infix_precedence;
} parser_rule;

/**
 * @param type
 * @return
 */
parser_rule *parser_get_rule(token_type type);

typedef struct {
  list_node *current;
  list_node *guard;
} parser_cursor;

ast_block_stmt parser(list *token_list);

ast_expr parser_expr();
ast_expr parser_literal();
ast_expr parser_unary();
ast_expr parser_grouping();
ast_expr parser_var();
ast_expr parser_call_expr(ast_expr name);
ast_expr parser_select(ast_expr left);
ast_expr parser_access(ast_expr left);
ast_expr parser_binary(ast_expr left);

ast_stmt parser_stmt();
ast_block_stmt parser_block();
ast_stmt parser_call_stmt();
ast_stmt parser_auto_infer_decl();
ast_stmt parser_var_or_function_decl();

ast_var_decl *parser_var_decl();
void parser_actual_param(ast_call *call);
void parser_formal_param(ast_function_decl *function_decl);

/**
 * foo.bar();
 * foo();
 * foo() {
 *
 * };
 * @return
 */
ast_stmt parser_null_function_decl();

/**
 * foo = 12
 * foo.bar = 12
 * foo[bar] = 12
 * @param ident
 * @return
 */
ast_stmt parser_assign();

void parser_current_reset();
token *parser_guard_advance();
token *parser_guard_peek();

bool parser_is(token_type t);
bool parser_next_is(uint8_t step, token_type t);
bool parser_is_call();
bool parser_is_custom_type();
bool parser_is_base_type();
bool parser_is_custom_type();
token *parser_must(token_type t);

#endif //NATURE_SRC_SYNTAX_PARSER_H_
