#ifndef NATURE_SRC_SYNTAX_PARSER_H_
#define NATURE_SRC_SYNTAX_PARSER_H_

#include "src/lib/list.h"
#include "src/ast.h"
#include "token.h"
#include <stdlib.h>

typedef enum {
    PRECEDENCE_NULL, // 最低优先级
    PRECEDENCE_ASSIGN, // 最低优先级
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

typedef struct {
    list_node *current;
} parser_cursor;

/**
 * @param type
 * @return
 */
parser_rule *parser_get_rule(token_type type);

ast_block_stmt parser(list *token_list);

ast_expr parser_expr();

ast_expr parser_precedence_expr(parser_precedence precedence);

ast_expr parser_literal();

ast_expr parser_unary();

ast_expr parser_grouping();

ast_expr parser_ident_expr();

ast_expr parser_call_expr(ast_expr left_expr);

ast_expr parser_select_property(ast_expr left);

ast_expr parser_access(ast_expr left);

ast_expr parser_binary(ast_expr left);

ast_expr parser_function_decl_expr(ast_type type);

ast_expr parser_new_struct(ast_type type);

ast_expr parser_new_list();

ast_expr parser_new_map();

ast_expr parser_direct_type_expr();

ast_expr parser_struct_type_expr();

ast_stmt parser_stmt();

ast_block_stmt parser_block();

ast_stmt parser_ident_stmt();

//ast_stmt parser_call_stmt();
ast_stmt parser_return_stmt();

ast_stmt parser_auto_infer_decl();

ast_stmt parser_var_or_function_decl();

ast_stmt parser_if_stmt();

ast_stmt parser_for_stmt();

ast_stmt parser_while_stmt();

ast_stmt parser_type_decl_stmt();

ast_block_stmt parser_else_if();

ast_new_fn *parser_function_decl(ast_type type);

ast_var_decl *parser_var_decl();

void parser_actual_param(ast_call *call);

void parser_formal_param(ast_new_fn *function_decl);

void parser_type_function_formal_param(ast_function_type_decl *type_function);

ast_type parser_type();

/**
 * foo = 12
 * foo.bar = 12
 * foo[bar] = 12
 * @param ident
 * @return
 */
ast_stmt parser_assign(ast_expr left);

token *parser_advance();

token *parser_peek();

int parser_line();

bool parser_consume(token_type t);

bool parser_is(token_type t);

bool parser_next_is(int step, token_type t);

list_node *parser_next(int step);

/**
 * 兼容 void
 * @return
 */
bool parser_is_direct_type();

bool parser_is_custom_type_var();

bool parser_is_simple_type();

token *parser_must(token_type t);

bool parser_must_stmt_end();

bool parser_is_function_decl(list_node *current);

void parser_cursor_init(list *token_list);

ast_stmt parser_new_stmt();

ast_expr parser_new_expr();

#endif //NATURE_SRC_SYNTAX_PARSER_H_
