#ifndef NATURE_SRC_LINEAR_H_
#define NATURE_SRC_LINEAR_H_

#include "value.h"
#include "ast.h"
#include "lib/table.h"

typedef struct {

} linear_operand_register;

typedef struct {
  string ident;
} linear_operand_var;

typedef struct {
  uint8_t count;
  linear_operand_var *var[UINT8_MAX];
} linear_operand_phi_body;

typedef enum {
  LINEAR_OPERAND_TYPE_VAR,
  LINEAR_OPERAND_TYPE_PHI_BODY,
} linear_operand_type;

typedef struct {
  uint8_t type;
  void *value;
} linear_operand;

typedef enum {
  LINEAR_OP_TYPE_ADD,
  LINEAR_OP_TYPE_PHI,
} linear_op_type;

// 四元组
typedef struct linear_op {
  uint8_t type;
  linear_operand first; // 参数1
  linear_operand second; // 参数2
  linear_operand result; // 参数3
  uint32_t number; // 编号
  struct linear_op *succ;
  struct linear_op *pred;
} linear_op;

typedef struct linear_basic_block {
  uint8_t label; // label 标号, 基本块编号， 和 label 区分一些
  linear_op *op; // 开始处的指令
  uint8_t operators_count;

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
  uint8_t count;
  linear_operand_var *vars[UINT8_MAX];
} linear_live_out, linear_ue_var, linear_var_kill;

// cfg 需要专门构造一个结尾 basic block 么，用来处理函数返回值等？其一定位于 blocks[count - 1]
typedef struct {
  // parent
  // children
  linear_ue_var ue_var_list[UINT8_MAX];
  linear_var_kill var_kill_list[UINT8_MAX];
  linear_live_out live_out_list[UINT8_MAX]; // 退出了基本块 N 后依然处于活跃状态的变量,用于构造剪枝 ssa
  linear_df df_list[UINT8_MAX]; // 支配边界
  linear_dom dom_list[UINT8_MAX]; // 实际数量受 block_count 控制, index 就是 label;
  linear_basic_block *idom[UINT8_MAX]; // index 就是 label
  // 所有节点的引用 (基于 ast 解析的中序遍历所得)
  uint8_t block_labels; // 基本块数量
  linear_basic_block *blocks[UINT8_MAX]; // 这里的 index 就是 block.label !!
  linear_basic_block *entry;
} closure;

closure *current;

linear_op *linear_new_op();
linear_operand_var *linear_clone_operand_var(linear_operand_var *var_operand);
linear_operand_phi_body *linear_new_phi_body(linear_operand_var *var_operand, uint8_t count);
void linear_closure(ast_closure_decl *closure);
void linear_call(ast_call_function *call);
void linear_block(ast_block_stmt *block);
void linear_binary(ast_binary_expr *binary);
void linear_literal(ast_literal *literal);
void linear_ident(ast_ident *ident);
void linear_if(ast_if_stmt *if_stmt);
void linear_while(ast_while_stmt *while_stmt);

#endif //NATURE_SRC_LINEAR_H_
