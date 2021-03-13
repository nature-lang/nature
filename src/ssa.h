#ifndef NATURE_SRC_SSA_H_
#define NATURE_SRC_SSA_H_

#include "lib/table.h"
#include "linear.h"

typedef struct {
  uint8_t numbers[UINT8_MAX];
  uint8_t count;
} var_counter_stack;

// 计算支配者
void ssa_dom(closure *c);
void ssa_idom(closure *c);
void ssa_df(closure *c);
void ssa_use_def(closure *c);
void ssa_live(closure *c);
linear_vars ssa_calc_live_out(closure *c, linear_basic_block *block);
linear_vars ssa_calc_live_in(closure *c, linear_basic_block *block);
bool ssa_live_changed(linear_vars *old, linear_vars *new);
bool ssa_phi_defined(linear_operand_var *var, linear_basic_block *block);
void ssa_add_phi(closure *c);
void ssa_rename(closure *c);

linear_dom ssa_calc_dom_blocks(closure *c, linear_basic_block *block);
bool ssa_dom_changed(linear_dom *old, linear_dom *new);
bool ssa_var_belong(linear_operand_var *var, linear_vars vars);
#endif //NATURE_SRC_SSA_H_
