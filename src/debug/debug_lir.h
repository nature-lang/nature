#ifndef NATURE_SRC_DEBUG_LIR_H_
#define NATURE_SRC_DEBUG_LIR_H_

#include "src/lir.h"

/**
 * MOVE int[1],NULL => t1
 * GOTO NULL,NULL => label[test]
 * @param operand
 * @return
 */
string lir_operand_to_string(lir_operand_t *operand);

string lir_label_to_string(lir_symbol_label_t *label);

string lir_var_to_string(lir_var_t *var);

string lir_imm_to_string(lir_imm_t *immediate);

string lir_addr_to_string(lir_indirect_addr_t *operand_addr);

string lir_formal_param_to_string(slice_t *formal_params);

string lir_actual_param_to_string(slice_t *actual_params);

string lir_vars_to_string(slice_t *vars);

#endif //NATURE_SRC_DEBUG_DEBUG_LIR_H_
