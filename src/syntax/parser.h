#ifndef NATURE_SRC_SYNTAX_PARSER_H_
#define NATURE_SRC_SYNTAX_PARSER_H_

#include "src/lib/list.h"
#include "src/ast/ast.h"
#include "token.h"
#include <stdlib.h>

typedef struct {
  list_node *current;
  list_node *guard;
} parser_cursor;

ast_block_stmt parser(list *token_list);
ast_stmt parser_stmt();
ast_expr parser_expr();
ast_block_stmt parser_block();

ast_stmt parser_auto_var_decl();
ast_stmt parser_type_var_decal();

void parser_current_reset();
token *parser_guard_advance();
bool parser_is(uint8_t t);
token *parser_must(uint8_t t);

#endif //NATURE_SRC_SYNTAX_PARSER_H_
