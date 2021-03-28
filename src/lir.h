#ifndef NATURE_SRC_LIR_H_
#define NATURE_SRC_LIR_H_

#include "value.h"
#include "ast.h"
#include "lib/table.h"
#include "register/intervals.h"

typedef struct {

} lir_operand_register;

typedef struct {
  string ident;
  string old;
} lir_operand_var;

typedef struct lir_vars {
  uint8_t count;
  lir_operand_var *vars[UINT8_MAX];
} lir_vars;

typedef struct {
  uint8_t rename_count;
  lir_vars vars;
} lir_operand_phi_body;

typedef enum {
  LINEAR_OPERAND_TYPE_VAR,
  LINEAR_OPERAND_TYPE_PHI_BODY,
} lir_operand_type;

typedef struct {
  uint8_t type;
  void *value;
} lir_operand;

typedef enum {
  LINEAR_OP_TYPE_ADD,
  LINEAR_OP_TYPE_PHI,
} lir_op_type;

// 四元组
typedef struct lir_op {
  uint8_t type;
  lir_operand first; // 参数1
  lir_operand second; // 参数2
  lir_operand result; // 参数3
  uint32_t number; // 编号
  struct lir_op *succ;
  struct lir_op *pred;
} lir_op;

typedef struct {
  uint8_t count;
  struct lir_basic_block *list[UINT8_MAX];
} lir_blocks;

typedef struct {
  uint8_t flag;
  uint8_t tree_high;
  uint8_t index_list[UINT8_MAX];
  uint8_t index;
  uint8_t depth;
} loop_detection;

typedef struct lir_basic_block {
  uint8_t label; // label 标号, 基本块编号， 和 label 区分一些
  lir_op *op; // 开始处的指令
  uint8_t operators_count;

  lir_blocks preds;
  lir_blocks succs;

  lir_vars use;
  lir_vars def;
  lir_vars live_out;
  lir_vars live_in;
  lir_blocks dom; // 当前块被哪些基本块支配
  lir_blocks df;
  lir_blocks be_idom; // 哪些块已当前块作为最近支配块,其组成了支配者树
  struct lir_basic_block *idom; // 当前块的最近支配者

  // loop detection
  loop_detection loop;
} lir_basic_block;

// cfg 需要专门构造一个结尾 basic block 么，用来处理函数返回值等？其一定位于 blocks[count - 1]
typedef struct {
  // parent
  // children
  lir_vars globals; // closure 中定义的变量列表
  uint8_t block_labels; // 基本块数量
//  lir_basic_block *blocks[UINT8_MAX]; // post order, 这里的 index 就是 block.label !!
  lir_blocks blocks; // 啥顺序呢？
  lir_basic_block *entry; // 基本块入口

  // 特定排序 block list
} closure;

closure *current;

lir_op *lir_new_op();
lir_operand_var *lir_clone_operand_var(lir_operand_var *var);
lir_operand_phi_body *lir_new_phi_body(lir_operand_var *var, uint8_t count);
void lir_closure(ast_closure_decl *closure);
void lir_call(ast_call_function *call);
void lir_block(ast_block_stmt *block);
void lir_binary(ast_binary_expr *binary);
void lir_literal(ast_literal *literal);
void lir_ident(ast_ident *ident);
void lir_if(ast_if_stmt *if_stmt);
void lir_while(ast_while_stmt *while_stmt);
string lir_label_to_string(uint8_t label);

#endif //NATURE_SRC_LIR_H_
