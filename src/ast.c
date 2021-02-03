#include "ast.h"

void ast_init_block_statement(ast_block_stat *block) {
  block->count = 0;
  block->capacity = 0;
  block->list = NULL;
}

void ast_insert_block_statement(ast_block_stat *block, ast_stat statement) {
  if (block->capacity <= block->count) {
    block->capacity = GROW_CAPACITY(block->capacity);
    block->list = (ast_stat *) realloc(block->list, sizeof(statement) * block->capacity);
  }

  // *(block->list+block->count) = 解引用之后的数据，所以是 statement 而不能是 statement 的指针 ！！！
  block->list[block->count++] = statement;
}
