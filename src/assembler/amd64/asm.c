#include "asm.h"

asm_operand_t *asm_symbol_operand(asm_inst_t asm_inst) {
  for (int i = 0; i < asm_inst.count; ++i) {
    asm_operand_t *operand = asm_inst.operands[i];
    if (operand->type == ASM_OPERAND_TYPE_SYMBOL) {
      return operand;
    }
  }
  return NULL;
}
