#include "ast.h"

void ast_init_block_stmt(ast_block_stmt *block) {
  block->count = 0;
  block->capacity = 0;
  block->list = NULL;
}

void ast_insert_block_stmt(ast_block_stmt *block, ast_stmt stmt) {
  if (block->capacity <= block->count) {
    block->capacity = GROW_CAPACITY(block->capacity);
    block->list = (ast_stmt *) realloc(block->list, sizeof(stmt) * block->capacity);
  }

  // *(block->list+block->count) = 解引用之后的数据，所以是 stmt 而不能是 stmt 的指针 ！！！
  block->list[block->count++] = stmt;
}
