#ifndef NATURE_SRC_DEBUG_DEBUG_LIR_H_
#define NATURE_SRC_DEBUG_DEBUG_LIR_H_

#include "src/lir/lir.h"

/**
 * MOVE int[1],NULL => t1
 * GOTO NULL,NULL => label[test]
 * @param operand
 * @return
 */
string lir_operand_to_string(lir_operand *operand);

string lir_operand_label_to_string(lir_operand_label *label);

string lir_operand_var_to_string(lir_operand_var *var);

string lir_operand_imm_to_string(lir_operand_immediate *immediate);

string lir_operand_memory_to_string(lir_operand_memory *operand_memory);

string lir_operand_actual_param_to_string(lir_operand_actual_param *actual_param);

string lir_operand_phi_body_to_string(lir_operand_phi_body *phi_body);

#endif //NATURE_SRC_DEBUG_DEBUG_LIR_H_
