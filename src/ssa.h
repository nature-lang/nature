#ifndef NATURE_SRC_SSA_H_
#define NATURE_SRC_SSA_H_

#include "lib/table.h"
#include "linear.h"


// 计算支配者

void ssa_dom(closure *c);
void ssa_idom(closure *c);
linear_dom ssa_calc_dom_blocks(closure *c, linear_basic_block *block);
bool ssa_dom_changed(linear_dom *old, linear_dom *new);
#endif //NATURE_SRC_SSA_H_
