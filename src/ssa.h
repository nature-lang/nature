#ifndef NATURE_SRC_SSA_H_
#define NATURE_SRC_SSA_H_

#include "utils/table.h"
#include "lir.h"

#define OPERAND_VAR_USE(_vars) \
    if (vars->count > 0) {\
        SLICE_FOR(vars) {\
            lir_var_t*var = SLICE_VALUE(vars);\
            bool is_def = ssa_var_belong(var, def);\
            if (!is_def && !table_exist(exist_use, var->ident)) {\
            slice_push(use, var);\
            table_set(exist_use, var->ident, var);\
        }\
        if (!table_exist(exist_var, var->ident)) {\
            slice_push(c->globals, var);\
            table_set(exist_var, var->ident, var);\
        }\
    }\
}\



typedef struct {
    uint8_t numbers[UINT8_MAX];
    uint8_t count;
} var_number_stack;

void ssa(closure_t *c);

// 计算支配者
void ssa_dom(closure_t *c);

void ssa_idom(closure_t *c);

bool ssa_is_idom(slice_t *dom, basic_block_t *await);

void ssa_df(closure_t *c);

void ssa_use_def(closure_t *c);

void ssa_live(closure_t *c);

slice_t *ssa_calc_live_out(closure_t *c, basic_block_t *block);

slice_t *ssa_calc_live_in(closure_t *c, basic_block_t *block);

bool ssa_live_changed(slice_t *old, slice_t *new);

bool ssa_phi_defined(lir_var_t *var, basic_block_t *block);

void ssa_add_phi(closure_t *c);

void ssa_rename(closure_t *c);

void ssa_rename_block(basic_block_t *block, table_t *var_number_table, table_t *stack_table);

uint8_t ssa_new_var_number(lir_var_t *var, table_t *var_number_table, table_t *stack_table);

void ssa_rename_var(lir_var_t *var, uint8_t number);

slice_t *ssa_calc_dom_blocks(closure_t *c, basic_block_t *block);

bool ssa_dom_changed(slice_t *old_dom, slice_t *new_dom);

bool ssa_var_belong(lir_var_t *var, slice_t *vars);

lir_var_t *ssa_phi_body_of(slice_t *phi_body, slice_t *preds, basic_block_t *guide);

void live_add(table_t *t, slice_t *lives, lir_var_t *var);

void live_remove(table_t *t, slice_t *lives, lir_var_t *var);

#endif //NATURE_SRC_SSA_H_
