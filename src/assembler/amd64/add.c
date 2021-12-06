#include "add.h"

elf_text_item add_reg64_to_reg64(asm_inst mov_inst) {
  byte opcode = 0x01;
  return reg64_to_reg64(mov_inst, opcode);
}

elf_text_item add_imm32_to_reg(asm_inst mov_inst) {
  byte opcode = 0x81;
  return imm32_to_reg64(mov_inst, opcode);
}

/**
 * ADD RAX,imm32
 * REX.W + 05 id
 * @param mov_inst
 * @return
 */
elf_text_item add_imm32_to_rax(asm_inst mov_inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  byte rex = 0b01001000;
  byte opcode = 0x05;

  result.data[i++] = rex;
  result.data[i++] = opcode;

  asm_imm *src_imm = mov_inst.src;

  // imm 部分
  NUM32_TO_DATA((int32_t) src_imm->value);

  SET_OFFSET(result)

  return result;
}

elf_text_item asm_inst_add_lower(asm_inst mov_inst) {
  if (mov_inst.size == 64) {
    if (is_imm(mov_inst.src_type) && is_reg(mov_inst.dst_type)) {
      if (is_rax(mov_inst.dst)) {
        return add_imm32_to_rax(mov_inst);
      }
      return add_imm32_to_reg(mov_inst);
    }

    if (is_reg(mov_inst.src_type) && is_reg(mov_inst.dst_type)) {
      return add_reg64_to_reg64(mov_inst);
    }
  }
  error_exit(0, "can not parser mov type");
  exit(0);
}
