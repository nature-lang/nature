#ifndef NATURE_SRC_LIR_H_
#define NATURE_SRC_LIR_H_

#include <src/lib/list.h>
#include "value.h"
#include "src/ast/ast.h"
#include "lib/table.h"
#include "register/register.h"

//typedef struct {
//  string ident;
//} lir_operand_reg;

typedef struct {
  string ident;
  string old;
} lir_operand_var;

typedef struct lir_vars {
  uint8_t count;
  lir_operand_var *list[UINT8_MAX];
} lir_vars;

//typedef struct lir_regs {
//  uint8_t count;
//  lir_operand_reg *list[UINT8_MAX];
//} lir_regs;

typedef struct {
  uint8_t rename_count;
  lir_vars vars;
} lir_operand_phi_body;

typedef enum {
  LIR_OPERAND_TYPE_VAR,
  LIR_OPERAND_TYPE_REG,
  LIR_OPERAND_TYPE_PHI_BODY,
  LIR_OPERAND_TYPE_LABEL,
} lir_operand_type;

typedef struct {
  uint8_t type;
  void *value;
} lir_operand;

typedef enum {
  LIR_OP_TYPE_ADD,
  LIR_OP_TYPE_PHI,
} lir_op_type;

// 四元组
typedef struct lir_op {
  uint8_t type;
  lir_operand first; // 参数1
  lir_operand second; // 参数2 (参数可能是固定寄存器)
  lir_operand result; // 参数3
  int id; // 编号
  struct lir_op *succ;
  struct lir_op *pred;
} lir_op;

typedef struct {
  uint8_t count;
  struct lir_basic_block *list[UINT8_MAX];
} lir_basic_blocks;

typedef struct {
  uint8_t flag;
  uint8_t tree_high;
  uint8_t index_list[UINT8_MAX];
  uint8_t index;
  uint8_t depth;
} loop_detection;

typedef struct lir_basic_block {
  uint8_t label; // label 标号, 基本块编号， 和 op_label 还是要稍微区分一下
  lir_op *first_op; // 开始处的指令
  lir_op *last_op;
  uint8_t operators_count;

  lir_basic_blocks preds;
  lir_basic_blocks succs;
  lir_basic_blocks forward_succs;
  uint8_t incoming_forward_count; // 正向进入到该节点的节点数量

  lir_vars use;
  lir_vars def;
  lir_vars live_out;
  lir_vars live_in;
  lir_basic_blocks dom; // 当前块被哪些基本块支配
  lir_basic_blocks df;
  lir_basic_blocks be_idom; // 哪些块已当前块作为最近支配块,其组成了支配者树
  struct lir_basic_block *idom; // 当前块的最近支配者

  // loop detection
  loop_detection loop;
} lir_basic_block;

// cfg 需要专门构造一个结尾 basic block 么，用来处理函数返回值等？其一定位于 blocks[count - 1]
typedef struct {
  // parent
  // children
  lir_vars globals; // closure 中定义的变量列表
  regs fixed_regs; // 作为临时寄存器使用到的寄存器
  lir_basic_blocks blocks; // 根据解析顺序得到

  list *operates; // 指令列表

  lir_basic_block *entry; // 基本块入口
  lir_basic_blocks order_blocks; // 寄存器分配前根据权重进行重新排序
  table *interval_table; // key包括 fixed register name 和 variable.ident
} closure;

lir_op *lir_new_op();
lir_operand_var *lir_clone_operand_var(lir_operand_var *var);
lir_operand_phi_body *lir_new_phi_body(lir_operand_var *var, uint8_t count);
lir_basic_block *lir_new_basic_block();
string lir_label_to_string(uint8_t label);
void lir_closure(ast_closure_decl *closure);
void lir_call(ast_call_function *call);
list *lir_ast_block(ast_block_stmt *block);
list *lir_var_decl(ast_var_decl_stmt *stmt);
void lir_binary(ast_binary_expr *binary);
void lir_literal(ast_literal *literal);
void lir_ident(ast_ident *ident);
void lir_if(ast_if_stmt *if_stmt);
void lir_while(ast_while_stmt *while_stmt);

#endif //NATURE_SRC_LIR_H_
