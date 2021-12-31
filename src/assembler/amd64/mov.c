#include "mov.h"

static elf_text_item mov_direct_addr_with_rax(asm_direct_addr *direct_addr, byte opcode) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  // REX.W => 48H (0100 1000)
  byte rex = 0b01001000;

  result.data[i++] = rex;
  result.data[i++] = opcode;

  NUM64_TO_DATA((int64_t) direct_addr->addr);

  result.offset = current_text_offset;
  result.size = i;
  current_text_offset += i;

  return result;
}

/**
 * mov rax,[rcx]
 * mov rax,[rcx+8]
 * mov rax,[rcx-8]
 * @param inst
 * @return
 */
static elf_text_item mov_indirect_addr_to_reg64(asm_inst inst) {
  asm_reg *reg = inst.dst;
  asm_indirect_addr *indirect_addr = inst.src;
  byte opcode = 0x8B;

  return indirect_addr_with_reg64(reg, indirect_addr, opcode, inst);
}

/**
 * @param inst
 * @return
 */
static elf_text_item mov_reg64_to_indirect_addr(asm_inst inst) {
  asm_reg *reg = inst.src;
  asm_indirect_addr *indirect_addr = inst.dst;
  byte opcode = 0x89;

  return indirect_addr_with_reg64(reg, indirect_addr, opcode, inst);
}

/**
 * REX.W + B8+ rd io =>  MOV imm64 to r64
 *
 * @param inst
 * @return
 */
static elf_text_item mov_imm64_to_reg64(asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;
  asm_reg *dst_reg = inst.dst;
  asm_imm *src_imm = inst.src;

  byte rex = 0b01001000; // 48H,由于未使用 ModR/w 部分，所以 opcode = opcode + reg
  byte opcode = 0xB8 + reg_to_number(dst_reg->name);   // opcode, intel 手册都是 16 进制的，所以使用 16 进制表示比较直观，其余依旧使用 2 进制表示
  result.data[i++] = rex;
  result.data[i++] = opcode;

  // imm 部分使用小端排序
  NUM64_TO_DATA((int64_t) src_imm->value);

  RETURN_FILL_RESULT(result)
}

/**
 * REX.W + C7 /0 id => MOV imm32 to r/m64
 * x64 对于立即数默认使用 imm32 处理，只有立即数的 zie 超过 32 位时才使用 64 位处理
 * @param inst
 * @return
 */
static elf_text_item mov_imm32_to_reg64(asm_inst inst) {
  byte opcode = 0xC7;
  return imm32_to_reg64(inst, opcode);
}

/**
 * mov r64 => r/m64 89H
 * @return
 */
static elf_text_item mov_reg64_to_reg64(asm_inst inst) {
  return reg64_to_reg64(inst, 0x89);
}

elf_text_item asm_inst_mov_lower(asm_inst inst) {
  // 根据 src 进行的区别进行处理
  /**
   * mov r64 => r64
   * mov r64 => m64
   * mov m64 => r64
   * mov imm64 => r64
   * cannot mem to mem/ imm to mem
   */
  if (inst.size == 64) {
    if (is_reg(inst.src_type) && is_reg(inst.dst_type)) {
      return mov_reg64_to_reg64(inst);
    } else if (is_indirect_addr(inst.dst_type) && is_reg(inst.src_type)) {
      return mov_reg64_to_indirect_addr(inst);
    } else if (is_indirect_addr(inst.src_type) && is_reg(inst.dst_type)) {
      return mov_indirect_addr_to_reg64(inst);
    } else if (is_imm(inst.src_type) && is_reg(inst.dst_type)) {
      // 32 位 和 64 位分开处理
      size_t imm = ((asm_imm *) inst.src)->value;
      if (imm > INT32_MAX) {
        return mov_imm64_to_reg64(inst);
      }

      return mov_imm32_to_reg64(inst);
    }
  }

  error_exit(0, "can not parser mov type");
  exit(0);
}
