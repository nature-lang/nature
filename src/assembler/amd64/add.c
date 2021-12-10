#include "add.h"

elf_text_item add_reg64_to_reg64(asm_inst inst) {
  byte opcode = 0x01;
  return reg64_to_reg64(inst, opcode);
}

elf_text_item add_imm32_to_reg(asm_inst inst) {
  byte opcode = 0x81;
  return imm32_to_reg64(inst, opcode);
}

/**
 * ADD RAX,imm32
 * REX.W + 05 id
 * @param inst
 * @return
 */
elf_text_item add_imm32_to_rax(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  byte rex = 0b01001000;
  byte opcode = 0x05;

  result.data[i++] = rex;
  result.data[i++] = opcode;

  asm_imm *src_imm = inst.src;

  // imm 部分
  NUM32_TO_DATA((int32_t) src_imm->value);

  SET_OFFSET(result)

  return result;
}

/**
 * @param inst
 * @return
 */
static elf_text_item add_reg64_to_indirect_addr(asm_inst inst) {
  asm_reg *reg = inst.src;
  asm_indirect_addr *indirect_addr = inst.dst;
  byte opcode = 0x01;

  return indirect_addr_with_reg64(reg, indirect_addr, opcode);
}

/**
 * @param inst
 * @return
 */
static elf_text_item add_indirect_addr_to_reg64(asm_inst inst) {
  asm_indirect_addr *indirect_addr = inst.src;
  asm_reg *reg = inst.dst;
  byte opcode = 0x03;

  return indirect_addr_with_reg64(reg, indirect_addr, opcode);
}

elf_text_item asm_inst_add_lower(asm_inst inst) {
  if (inst.size == 64) {
    if (is_imm(inst.src_type) && is_reg(inst.dst_type)) {
      if (is_rax(inst.dst)) {
        return add_imm32_to_rax(inst);
      }
      return add_imm32_to_reg(inst);
    }

    if (is_reg(inst.src_type) && is_reg(inst.dst_type)) {
      return add_reg64_to_reg64(inst);
    }

    if (is_indirect_addr(inst.dst_type) && is_reg(inst.src_type)) {
      return add_reg64_to_indirect_addr(inst);
    }

    if (is_indirect_addr(inst.src_type) && is_reg(inst.dst_type)) {
      return add_indirect_addr_to_reg64(inst);
    }
  }
  error_exit(0, "can not parser mov type");
  exit(0);
}
