#include "set.h"

static byte set_type_second_code(asm_op_type op) {
  switch (op) {
    case ASM_OP_TYPE_SETG:return 0x9F;
    case ASM_OP_TYPE_SETGE:return 0x9D;
    case ASM_OP_TYPE_SETE:return 0x94;
    case ASM_OP_TYPE_SETNE:return 0x95;
    case ASM_OP_TYPE_SETL:return 0x9C;
    case ASM_OP_TYPE_SETLE:return 0x9E;
    default: {
      error_exit(0, "cannot identify set op(%d)", op);
    }
  }
}

static elf_text_item set_indirect_addr(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;
//  result.data[i++] = 0b01001000; // rex.w
  result.data[i++] = 0x0F;
  result.data[i++] = set_type_second_code(inst.op);

  asm_indirect_addr *indirect_addr = inst.dst;
  byte modrm = indirect_disp_mod(indirect_addr->offset);
  modrm |= reg_to_number(indirect_addr->reg);
  result.data[i++] = modrm;

  INDIRECT_OFFSET_TO_DATA(modrm, indirect_addr->offset);

  RETURN_FILL_RESULT(result);
}

/**
 * 暂时不支持需要 rex.b 修饰的寄存器
 * @param inst
 * @return
 */
static elf_text_item set_reg(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;
//  result.data[i++] = 0b01001000; // rex.w
  result.data[i++] = 0x0F;
  result.data[i++] = set_type_second_code(inst.op);

  byte modrm = 0b11000000;
  asm_reg *dst_reg = inst.dst;
  modrm |= reg_to_number(dst_reg->name);
  result.data[i++] = modrm;

  RETURN_FILL_RESULT(result);
}

elf_text_item asm_inst_set_lower(asm_inst inst) {
  if (inst.size == 8) {
    if (is_reg(inst.dst_type)) {
      return set_reg(inst);
    }
    if (is_indirect_addr(inst.dst_type)) {
      return set_indirect_addr(inst);
    }
  }
  error_exit(0, "can not parser mov type");
  exit(0);
}
