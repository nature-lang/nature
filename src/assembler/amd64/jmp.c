#include "jmp.h"

/**
 * FF /4
 * @param inst
 * @return
 */
static elf_text_item jmp_indirect_addr(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;
  result.data[i++] = 0xFF;

  asm_indirect_addr *indirect_addr = inst.dst;
  byte modrm = indirect_disp_mod(indirect_addr->offset);
  // reg
  modrm |= 4 << 3;
  // r/m
  modrm |= reg_to_number(indirect_addr->reg);
  result.data[i++] = modrm;

  INDIRECT_OFFSET_TO_DATA(modrm, indirect_addr->offset);

  RETURN_FILL_RESULT(result)
}

static elf_text_item jmp_reg64(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;
  result.data[i++] = 0xFF;

  byte modrm = 0b11000000; // ModR/M mod(11 表示 r/m confirm to reg)
  asm_reg *reg = inst.dst;
  modrm |= 4 << 3; // /4 扩展操作码
  modrm |= reg_to_number(reg->name); // r/m 部分
  result.data[i++] = modrm;

  RETURN_FILL_RESULT(result);
}

static elf_text_item jmp_rel32(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  result.data[i++] = 0xE9; // jmp 32

  // symbol 目前还是无法解析的
//  asm_symbol *symbol = inst.dst;

  // 使用 nop 占位
  result.data[i++] = 0x90; // nop
  result.data[i++] = 0x90; // nop
  result.data[i++] = 0x90; // nop
  result.data[i++] = 0x90; // nop

  RETURN_FILL_RESULT(result);
}

elf_text_item asm_inst_jmp_lower(asm_inst inst) {
  if (inst.size == 64) {
    if (is_reg(inst.dst_type)) {
      return jmp_reg64(inst);
    }

    if (is_indirect_addr(inst.dst_type)) {
      return jmp_indirect_addr(inst);
    }

    if (is_symbol(inst.dst_type)) {
      return jmp_rel32(inst);
    }
  }
  error_exit(0, "can not parser mov type");
  exit(0);
}
