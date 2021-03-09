#ifndef NATURE_SRC_LINEAR_H_
#define NATURE_SRC_LINEAR_H_

#include "value.h"
#include "ast.h"
#include "lib/table.h"

typedef struct {
  uint8_t type;
  void *value;
} linear_operand;

typedef struct {
  linear_operand left;
  linear_operand right;
  linear_operand result;
} linear_op_add;

typedef struct {
  string type;
  void *operator;
  uint32_t number;
} linear_op;

typedef struct linear_basic_block {
  uint8_t label; // label 标号
  linear_op operators[UINT8_MAX];
  uint8_t preds_count;
  uint8_t succs_count;
  struct linear_basic_block *preds[UINT8_MAX];
  struct linear_basic_block *succs[UINT8_MAX];
} linear_basic_block;

typedef struct {
  uint8_t count;
  linear_basic_block *blocks[UINT8_MAX];
} linear_dom, linear_df;

typedef struct {
  // parent
  // children
  linear_df df_list[UINT8_MAX]; // 支配边界
  linear_dom dom_list[UINT8_MAX]; // 实际数量受 block_count 控制, index 就是 label;
  linear_basic_block *idom[UINT8_MAX]; // index 就是 label
  // 所有节点的引用 (基于 ast 解析的中序遍历所得)
  uint8_t block_labels;
  linear_basic_block *blocks[UINT8_MAX]; // 这里的 index 就是 label !!
  linear_basic_block *entry;
} closure;

closure *current;

void linear_closure(ast_closure_decl *closure);
void linear_call(ast_call_function *call);
void linear_block(ast_block_stmt *block);
void linear_binary(ast_binary_expr *binary);
void linear_literal(ast_literal *literal);
void linear_ident(ast_ident *ident);
void linear_if(ast_if_stmt *if_stmt);
void linear_while(ast_while_stmt *while_stmt);

#endif //NATURE_SRC_LINEAR_H_
