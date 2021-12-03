#include "add.h"

/**
 * REX.W + 05 id
 * @param mov_inst
 * @return
 */
elf_text_item inst_add_imm32_to_rax(asm_inst mov_inst) {
  byte rex = 0b01001000;
  byte opcode = 0x05;
  // imm 部分
}

elf_text_item inst_add_lower(asm_inst mov_inst) {
  elf_text_item result;
  return result;
}
