#include "mov.h"

static elf_text_item mov_direct_addr_with_rax(asm_direct_addr *direct_addr, byte opcode) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  // REX.W => 48H (0100 1000)
  byte rex = 0b01001000;

  result.data[i++] = rex;
  result.data[i++] = opcode;

  // disp32 处理
  int64_t addr = direct_addr->addr;
  result.data[i++] = (int8_t) addr;
  result.data[i++] = (int8_t) (addr >> 8);
  result.data[i++] = (int8_t) (addr >> 16);
  result.data[i++] = (int8_t) (addr >> 24);
  result.data[i++] = (int8_t) (addr >> 32);
  result.data[i++] = (int8_t) (addr >> 40);
  result.data[i++] = (int8_t) (addr >> 48);
  result.data[i++] = (int8_t) (addr >> 56);

  result.offset = current_text_offset;
  result.size = i;
  current_text_offset += i;

  return result;
}

static elf_text_item mov_direct_addr_to_rax(asm_inst mov_inst) {
  asm_direct_addr *direct_addr = mov_inst.src;
  byte opcode = 0xA1;
  return mov_direct_addr_with_rax(direct_addr, opcode);
}

static elf_text_item mov_rax_to_direct_addr(asm_inst mov_inst) {
  asm_direct_addr *direct_addr = mov_inst.dst;
  byte opcode = 0xA3;
  return mov_direct_addr_with_rax(direct_addr, opcode);
}

static byte indirect_disp_mod(int64_t offset) {
  if (offset == 0) {
    return 0b00000000;
  }

  // 8bit
  if (IN_INT8(offset)) {
    return 0b01000000;
  }

  return 0b10000000;
}

static elf_text_item mov_indirect_addr_with_reg64(asm_reg *reg, asm_indirect_addr *indirect_addr, byte opcode) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  int64_t offset = indirect_addr->offset;
  if (offset > INT32_MAX || offset < INT32_MIN) {
    error_exit(0, "offset %d to large", offset);
  }

  // REX.W => 48H (0100 1000)
  byte rex = 0b01001000;
  byte modrm = indirect_disp_mod(indirect_addr->offset);
  // reg
  modrm |= (reg_to_number(reg->name) << 3);
  // r/m
  modrm |= reg_to_number(indirect_addr->reg);

  result.data[i++] = rex;
  result.data[i++] = opcode;
  result.data[i++] = modrm;

  // disp 如果 disp 为负数，c 语言已经使用了补码表示，所以直接求小端即可
  if ((modrm & 0b11000000) != 0b00000000) {
    result.data[i++] = (int8_t) offset;

    // int32
    if ((modrm & 0b11000000) == 0b10000000) {
      result.data[i++] = (int8_t) (offset >> 8);
      result.data[i++] = (int8_t) (offset >> 16);
      result.data[i++] = (int8_t) (offset >> 24);
    }
  }

  result.offset = current_text_offset;
  result.size = i;
  current_text_offset += i;

  return result;
}

/**
 * mov rax,[rcx]
 * mov rax,[rcx+8]
 * mov rax,[rcx-8]
 * @param mov_inst
 * @return
 */
static elf_text_item mov_indirect_addr_to_reg64(asm_inst mov_inst) {
  asm_reg *reg = mov_inst.dst;
  asm_indirect_addr *indirect_addr = mov_inst.src;
  byte opcode = 0x8B;

  return mov_indirect_addr_with_reg64(reg, indirect_addr, opcode);
}

/**
 * @param mov_inst
 * @return
 */
static elf_text_item mov_reg64_to_indirect_addr(asm_inst mov_inst) {
  asm_reg *reg = mov_inst.src;
  asm_indirect_addr *indirect_addr = mov_inst.dst;
  byte opcode = 0x89;

  return mov_indirect_addr_with_reg64(reg, indirect_addr, opcode);
}

/**
 * @param mov_inst
 * @return
 */
static elf_text_item mov_direct_addr_with_reg64(asm_reg *reg, asm_direct_addr *direct_addr, byte opcode) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;


//  asm_reg *src_reg = mov_inst.src;
//  asm_direct_addr *direct_addr = mov_inst.dst;
//
//  byte opcode = 0x89;

  // REX.W => 48H (0100 1000)
  byte rex = 0b01001000;
  byte modrm = 0b00000101; // mod:00 + r/m:101 => 32 位直接寻址
  // reg
  modrm |= (reg_to_number(reg->name) << 3);

  result.data[i++] = rex;
  result.data[i++] = opcode;
  result.data[i++] = modrm;

  // disp32 处理
  int32_t addr = (int32_t) direct_addr->addr;
  result.data[i++] = (int8_t) addr;
  result.data[i++] = (int8_t) (addr >> 8);
  result.data[i++] = (int8_t) (addr >> 16);
  result.data[i++] = (int8_t) (addr >> 24);

  result.offset = current_text_offset;
  result.size = i;
  current_text_offset += i;

  return result;
}

static elf_text_item mov_reg64_to_direct_addr(asm_inst mov_inst) {
  asm_reg *src_reg = mov_inst.src;
  asm_direct_addr *direct_addr = mov_inst.dst;
  byte opcode = 0x89;

  return mov_direct_addr_with_reg64(src_reg, direct_addr, opcode);
}

/**
 * 包括直接寻址，寄存器间接寻址，变址寻址
 * REX.W + 8B /r  => Move r/m64 to r64.
 *
 * @param mov_inst
 * @return
 */
static elf_text_item mov_direct_addr_to_reg64(asm_inst mov_inst) {
  asm_reg *dst_reg = mov_inst.dst;
  asm_direct_addr *direct_addr = mov_inst.src;
  byte opcode = 0x8B;

  return mov_direct_addr_with_reg64(dst_reg, direct_addr, opcode);
}

/**
 * REX.W + B8+ rd io =>  MOV imm64 to r64
 *
 * @param mov_inst
 * @return
 */
static elf_text_item mov_imm64_to_reg64(asm_inst mov_inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;
  asm_reg *dst_reg = mov_inst.dst;
  asm_imm *src_imm = mov_inst.src;

  byte rex = 0b01001000; // 48H,由于未使用 ModR/w 部分，所以 opcode = opcode + reg
  byte opcode = 0xB8 + reg_to_number(dst_reg->name);   // opcode, intel 手册都是 16 进制的，所以使用 16 进制表示比较直观，其余依旧使用 2 进制表示
  result.data[i++] = rex;
  result.data[i++] = opcode;

  // imm 部分使用小端排序
  int64_t imm = (int64_t) src_imm->value;
  result.data[i++] = (int8_t) imm;
  result.data[i++] = (int8_t) (imm >> 8);
  result.data[i++] = (int8_t) (imm >> 16);
  result.data[i++] = (int8_t) (imm >> 24);
  result.data[i++] = (int8_t) (imm >> 32);
  result.data[i++] = (int8_t) (imm >> 40);
  result.data[i++] = (int8_t) (imm >> 48);
  result.data[i++] = (int8_t) (imm >> 56);

  result.offset = current_text_offset;
  result.size = i;
  current_text_offset += i;

  return result;
}

/**
 * REX.W + C7 /0 id => MOV imm32 to r/m64
 * x64 对于立即数默认使用 imm32 处理，只有立即数的 zie 超过 32 位时才使用 64 位处理
 * @param mov_inst
 * @return
 */
static elf_text_item mov_imm32_to_reg64(asm_inst mov_inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  byte rex = 0b01001000;
  byte opcode = 0xC7;
  asm_reg *dst_reg = mov_inst.dst;
  asm_imm *src_imm = mov_inst.src;
  byte modrm = 0b11000000;
  modrm |= reg_to_number(dst_reg->name);

  result.data[i++] = rex;
  result.data[i++] = opcode;
  result.data[i++] = modrm;

  // 数字截取 32 位,并转成小端序
  int32_t imm = (int32_t) src_imm->value;
  result.data[i++] = (int8_t) (imm >> 24);
  result.data[i++] = (int8_t) (imm >> 16);
  result.data[i++] = (int8_t) (imm >> 8);
  result.data[i++] = (int8_t) imm;

  result.offset = current_text_offset;
  result.size = i;
  current_text_offset += i;

  return result;
}

/**
 * mov r64 => r/m64 89H
 * @return
 */
static elf_text_item mov_reg64_to_reg64(asm_inst mov_inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();

  uint8_t i = 0;

  // REX.W => 48H (0100 1000)
  result.data[i++] = 0b01001000; // rex.w
  result.data[i++] = 0x89; // opcode, intel 手册都是 16 进制的，所以使用 16 进制表示比较直观，其余依旧使用 2 进制表示
  byte modrm = 0b11000000; // ModR/M mod(11 表示 r/m confirm to reg) + reg + r/m
  asm_reg *src_reg = mov_inst.src;
  asm_reg *dst_reg = mov_inst.dst;
  modrm |= reg_to_number(src_reg->name) << 3; // reg
  modrm |= reg_to_number(dst_reg->name); // r/m
  result.data[i++] = modrm;

  result.offset = current_text_offset;
  result.size = i;
  current_text_offset += i;

  return result;
}

elf_text_item inst_mov_lower(asm_inst mov_inst) {
  // 根据 src 进行的区别进行处理
  /**
   * mov r64 => r64
   * mov r64 => m64
   * mov m64 => r64
   * mov imm64 => r64
   * cannot mem to mem/ imm to mem
   */
  if (mov_inst.size == 64) {
    if (is_reg(mov_inst.src_type) && is_reg(mov_inst.dst_type)) {
      return mov_reg64_to_reg64(mov_inst);
    } else if (is_direct_addr(mov_inst.dst_type) && is_reg(mov_inst.src_type)) {
      if (is_rax(mov_inst.src)) {
        return mov_rax_to_direct_addr(mov_inst);
      }
      return mov_reg64_to_direct_addr(mov_inst);
    } else if (is_indirect_addr(mov_inst.dst_type) && is_reg(mov_inst.src_type)) {
      return mov_reg64_to_indirect_addr(mov_inst);
    } else if (is_indirect_addr(mov_inst.src_type) && is_reg(mov_inst.dst_type)) {
      return mov_indirect_addr_to_reg64(mov_inst);
    } else if (is_direct_addr(mov_inst.src_type) && is_reg(mov_inst.dst_type)) {
      if (is_rax(mov_inst.dst)) {
        return mov_direct_addr_to_rax(mov_inst);
      }
      return mov_direct_addr_to_reg64(mov_inst);
    } else if (is_imm(mov_inst.src_type) && is_reg(mov_inst.dst_type)) {
      // 32 位 和 64 位分开处理
      size_t imm = ((asm_imm *) mov_inst.src)->value;
      if (imm > INT32_MAX) {
        return mov_imm64_to_reg64(mov_inst);
      }

      return mov_imm32_to_reg64(mov_inst);
    }
  }

  error_exit(0, "can not parser mov type");
}
