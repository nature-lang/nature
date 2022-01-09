#include "jcc.h"

/**
 * 0F 84 cd JE rel32
 * @param inst
 * @return
 */
static elf_text_item je_rel32(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  result.data[i++] = 0x0F;
  result.data[i++] = 0x84;

  // TODO 判断符号表是否有该符号

  result.data[i++] = 0x90; // nop
  result.data[i++] = 0x90; // nop
  result.data[i++] = 0x90; // nop
  result.data[i++] = 0x90; // nop

  RETURN_FILL_RESULT(result);
}

elf_text_item asm_inst_jcc_lower(asm_inst inst) {
  if (inst.size == 64) {
    if (inst.op == ASM_OP_TYPE_JE && is_symbol(inst.dst_type)) {
      return je_rel32(inst);
    }
  }
  error_exit(0, "can not parser inst type");
  exit(0);
}
