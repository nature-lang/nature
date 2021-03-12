#ifndef NATURE_SRC_SSA_H_
#define NATURE_SRC_SSA_H_

#include "lib/table.h"
#include "linear.h"

// 计算支配者
void ssa_dom(closure *c);
void ssa_idom(closure *c);
void ssa_df(closure *c);
void ssa_pre_live_out(closure *c);
void ssa_live_out(closure *c);
linear_live_out ssa_calc_live_out_vars(closure *c, linear_basic_block *block);
bool ssa_live_out_changed(linear_live_out *old, linear_live_out *new);
bool ssa_phi_defined(linear_operand_var *var, linear_basic_block *block);

linear_dom ssa_calc_dom_blocks(closure *c, linear_basic_block *block);
bool ssa_dom_changed(linear_dom *old, linear_dom *new);
bool ssa_var_belong(linear_operand_var *var, linear_operand_var *vars[UINT8_MAX], uint8_t count);
#endif //NATURE_SRC_SSA_H_
