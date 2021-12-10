#include "sub.h"

elf_text_item sub_reg64_to_reg64(asm_inst inst) {
  byte opcode = 0x29;
  return reg64_to_reg64(inst, opcode);
}

elf_text_item sub_imm32_to_reg(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  byte opcode = 0x81;
  byte rex = 0b01001000;
  asm_reg *dst_reg = inst.dst;
  asm_imm *src_imm = inst.src;

  byte modrm = 0b11000000;
  modrm |= (5 << 3);// reg = /5
  modrm |= reg_to_number(dst_reg->name); // r/m 部分

  result.data[i++] = rex;
  result.data[i++] = opcode;
  result.data[i++] = modrm;

  // 数字截取 32 位,并转成小端序
  NUM32_TO_DATA((int32_t) src_imm->value);

  SET_OFFSET(result);
  return result;
}

/**
 * ADD RAX,imm32
 * REX.W + 05 id
 * @param inst
 * @return
 */
elf_text_item sub_imm32_to_rax(asm_inst inst) {
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
static elf_text_item sub_reg64_to_indirect_addr(asm_inst inst) {
  asm_reg *reg = inst.src;
  asm_indirect_addr *indirect_addr = inst.dst;
  byte opcode = 0x01;

  return indirect_addr_with_reg64(reg, indirect_addr, opcode);
}

/**
 * @param inst
 * @return
 */
static elf_text_item sub_indirect_addr_to_reg64(asm_inst inst) {
  asm_indirect_addr *indirect_addr = inst.src;
  asm_reg *reg = inst.dst;
  byte opcode = 0x03;

  return indirect_addr_with_reg64(reg, indirect_addr, opcode);
}

elf_text_item asm_inst_sub_lower(asm_inst inst) {
  if (inst.size == 64) {
    if (is_imm(inst.src_type) && is_reg(inst.dst_type)) {
      if (is_rax(inst.dst)) {
        return sub_imm32_to_rax(inst);
      }
      return sub_imm32_to_reg(inst);
    }

    if (is_reg(inst.src_type) && is_reg(inst.dst_type)) {
      return sub_reg64_to_reg64(inst);
    }

    if (is_indirect_addr(inst.dst_type) && is_reg(inst.src_type)) {
      return sub_reg64_to_indirect_addr(inst);
    }

    if (is_indirect_addr(inst.src_type) && is_reg(inst.dst_type)) {
      return sub_indirect_addr_to_reg64(inst);
    }
  }
  error_exit(0, "can not parser mov type");
  exit(0);
}
