#include "mul.h"

/**
 * IMUL r64, r/m64
 * imul rdx,rax  // rdx * rax => rdx:rax
 */

elf_text_item mul_reg64_to_reg64(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();

  uint8_t i = 0;

  // REX.W => 48H (0100 1000)
  result.data[i++] = 0b01001000; // rex.w
  result.data[i++] = 0x0F;
  result.data[i++] = 0xAF;
  byte modrm = 0b11000000; // ModR/M mod(11 表示 r/m confirm to reg) + reg + r/m

  asm_reg *src_reg = inst.src;
  asm_reg *dst_reg = inst.dst;
  modrm |= reg_to_number(dst_reg->name) << 3; // reg
  modrm |= reg_to_number(src_reg->name); // r/m
  result.data[i++] = modrm;

  RETURN_FILL_RESULT(result);
}

/**
 * @param inst
 * @return
 */
static elf_text_item mul_indirect_addr_to_reg64(asm_inst inst) {
  asm_indirect_addr *indirect_addr = inst.src;
  asm_reg *reg = inst.dst;

  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  // REX.W => 48H (0100 1000)
  byte rex = 0b01001000;

  byte modrm = indirect_disp_mod(indirect_addr->offset);
  // reg
  modrm |= (reg_to_number(reg->name) << 3);
  // r/m
  modrm |= reg_to_number(indirect_addr->reg);

  result.data[i++] = rex;
  result.data[i++] = 0x0F;
  result.data[i++] = 0xAF;
  result.data[i++] = modrm;

  INDIRECT_OFFSET_TO_DATA(modrm, indirect_addr->offset);

  RETURN_FILL_RESULT(result);
}

elf_text_item asm_inst_mul_lower(asm_inst inst) {
  if (inst.size == 64) {
//    if (is_imm(inst.src_type) && is_reg(inst.dst_type)) {
//      if (is_rax(inst.dst)) {
//        return sub_imm32_to_rax(inst);
//      }
//      return sub_imm32_to_reg(inst);
//    }

    if (is_reg(inst.src_type) && is_reg(inst.dst_type)) {
      return mul_reg64_to_reg64(inst);
    }

    if (is_indirect_addr(inst.src_type) && is_reg(inst.dst_type)) {
      return mul_indirect_addr_to_reg64(inst);
    }
  }
  error_exit(0, "can not parser mov type");
  exit(0);
}