#include "ast.h"

ast_block_stmt ast_new_block_stmt() {
  ast_block_stmt result;
  result.count = 0;
  result.capacity = 0;
  result.list = NULL;
  return result;
}

void ast_block_stmt_push(ast_block_stmt *block, ast_stmt stmt) {
  if (block->capacity <= block->count) {
    block->capacity = GROW_CAPACITY(block->capacity);
    block->list = (ast_stmt *) realloc(block->list, sizeof(stmt) * block->capacity);
  }

  block->list[block->count++] = stmt;
}
ast_type ast_new_simple_type(type_category type) {
  ast_type result = {
      .is_origin = true,
      .value = NULL,
      .category = type
  };
  return result;
}

ast_ident *ast_new_ident(char *literal) {
  ast_ident *ident = malloc(sizeof(ast_ident));
  ident->literal = literal;
  return ident;
}

