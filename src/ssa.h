#ifndef NATURE_SRC_SSA_H_
#define NATURE_SRC_SSA_H_

#include "utils/table.h"
#include "src/lir/lir.h"

#define OPERAND_VAR_USR(_vars) \
if (_vars.count > 0) { \
    for (int i = 0; i < _vars.count; ++i) {\
        lir_operand_var *var = _vars.list[i]; \
        bool is_def = ssa_var_belong(var, def); \
        if (!is_def && !table_exist(exist_use, var->ident)) {\
            use.list[use.count++] = var; \
            table_set(exist_use, var->ident, var);\
        } \
        if (!table_exist(exist_var, var->ident)) {\
            c->globals.list[c->globals.count++] = var;\
            table_set(exist_var, var->ident, var); \
        } \
    } \
}  \



typedef struct {
    uint8_t numbers[UINT8_MAX];
    uint8_t count;
} var_number_stack;

void ssa(closure *c);

// 计算支配者
void ssa_dom(closure *c);

void ssa_idom(closure *c);

bool ssa_is_idom(slice_t *dom, lir_basic_block *await);

void ssa_df(closure *c);

void ssa_use_def(closure *c);

void ssa_live(closure *c);

lir_vars ssa_calc_live_out(closure *c, lir_basic_block *block);

lir_vars ssa_calc_live_in(closure *c, lir_basic_block *block);

bool ssa_live_changed(lir_vars *old, lir_vars *new);

bool ssa_phi_defined(lir_operand_var *var, lir_basic_block *block);

void ssa_add_phi(closure *c);

void ssa_rename(closure *c);

void ssa_rename_basic(lir_basic_block *block, table *var_number_table, table *stack_table);

uint8_t ssa_new_var_number(lir_operand_var *var, table *var_number_table, table *stack_table);

void ssa_rename_var(lir_operand_var *var, uint8_t number);

slice_t *ssa_calc_dom_blocks(closure *c, lir_basic_block *block);

bool ssa_dom_changed(slice_t *old_dom, slice_t *new_dom);

bool ssa_var_belong(lir_operand_var *var, lir_vars vars);

#endif //NATURE_SRC_SSA_H_
