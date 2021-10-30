#include "asm.h"
#include "src/value.h"
#include "string.h"
#include "src/lib/error.h"

void asm_init() {
  asm_insts.count = 0;
  asm_data.count = 0;
}

void asm_insts_push(asm_inst inst) {
  asm_insts.list[asm_insts.count++] = inst;
}

void asm_data_push(asm_var_decl var_decl) {
  asm_data.list[asm_data.count++] = var_decl;
}

static bool is_imm(asm_operand_type t) {
  if (t == ASM_OPERAND_TYPE_IMM) {
    return true;
  }
  return false;
}

static bool is_reg(asm_operand_type t) {
  if (t == ASM_OPERAND_TYPE_REG) {
    return true;
  }
  return false;
}

static bool is_addr(asm_operand_type t) {
  if (t == ASM_OPERAND_TYPE_DIRECT_ADDR || t == ASM_OPERAND_TYPE_INDEX_ADDR || t == ASM_OPERAND_TYPE_INDIRECT_ADDR) {
    return true;
  }
  return false;
}

/**
 * 取值 0~7
 * 0: 00000000 ax
 * 1: 00000001 cx
 * 2: 00000010 dx
 * 3: 00000011 bx
 * 4: 00000100 sp
 * 5: 00000101 bp
 * 6: 00000110 si
 * 7: 00000111 di
 * @return
 */
static byte reg_to_number(string reg) {
  if (strcmp(reg, "rax") == 0) {
    return 0;
  }
  if (strcmp(reg, "rcx") == 0) {
    return 1;
  }
  if (strcmp(reg, "rdx") == 0) {
    return 2;
  }
  if (strcmp(reg, "rbx") == 0) {
    return 3;
  }
  if (strcmp(reg, "rsp") == 0) {
    return 4;
  }
  if (strcmp(reg, "rbp") == 0) {
    return 5;
  }
  if (strcmp(reg, "rsi") == 0) {
    return 6;
  }
  if (strcmp(reg, "rdi") == 0) {
    return 7;
  }

  error_exit(0, "cannot parser '%s' reg", reg);
}

/**
 * REX.W + B8+ rd io MOV imm64 to r64
 *
 * @param mov_inst
 * @return
 */
static elf_text_item mov_imm64_to_reg64(asm_inst mov_inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  byte data[30];
  uint8_t i = 0;
  asm_reg *dst_reg = mov_inst.dst;
  asm_imm *src_imm = mov_inst.src;

  byte rex = 0b01001000; // 48H,由于未使用 ModR/w 部分，所以 opcode = opecode + reg
  byte opcode = 0xB8 + reg_to_number(dst_reg->name);   // opcode, intel 手册都是 16 进制的，所以使用 16 进制表示比较直观，其余依旧使用 2 进制表示
  data[i++] = rex;
  data[i++] = opcode;

  // imm 部分使用小端排序
  int64_t imm = (int64_t) src_imm->value;
  data[i++] = (int8_t) (imm >> 56);
  data[i++] = (int8_t) (imm >> 48);
  data[i++] = (int8_t) (imm >> 40);
  data[i++] = (int8_t) (imm >> 32);
  data[i++] = (int8_t) (imm >> 24);
  data[i++] = (int8_t) (imm >> 16);
  data[i++] = (int8_t) (imm >> 8);
  data[i++] = (int8_t) imm;

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
  byte data[30];
  uint8_t i = 0;

  byte rex = 0b01001000;
  byte opcode = 0xC7;
  asm_reg *dst_reg = mov_inst.dst;
  asm_imm *src_imm = mov_inst.src;
  byte modrm = 0b11000000;
  modrm |= reg_to_number(dst_reg->name);

  data[i++] = rex;
  data[i++] = opcode;
  data[i++] = modrm;

  // 数字截取 32 位,并转成小端序
  int32_t imm = (int32_t) src_imm->value;
  data[i++] = (int8_t) (imm >> 24);
  data[i++] = (int8_t) (imm >> 16);
  data[i++] = (int8_t) (imm >> 8);
  data[i++] = (int8_t) imm;

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

  byte data[30];
  uint8_t i = 0;

  // REX.W => 48H (0100 1000)
  data[i++] = 0b01001000; // rex.w
  data[i++] = 0x89; // opcode, intel 手册都是 16 进制的，所以使用 16 进制表示比较直观，其余依旧使用 2 进制表示
  byte modrm = 0b11000000; // ModR/M mod(11 表示 r/m confirm to reg) + reg + r/m
  asm_reg *src_reg = mov_inst.src;
  asm_reg *dst_reg = mov_inst.dst;
  modrm |= reg_to_number(src_reg->name) << 3; // reg
  modrm |= reg_to_number(dst_reg->name); // r/m
  data[i++] = modrm;

  result.offset = current_text_offset;
  result.size = i;
  current_text_offset += i;

  return result;
}

/**
 * @param mov_inst
 * @return
 */
static elf_text_item inst_mov_lower(asm_inst mov_inst) {
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
    } else if (is_reg(mov_inst.src_type) && is_addr(mov_inst.dst_type)) {

    } else if (is_addr(mov_inst.src_type) && is_reg(mov_inst.dst_type)) {

    } else if (is_imm(mov_inst.src_type) && is_reg(mov_inst.dst_type)) {
      // 32 位 和 64 位分开处理
      int32_t imm = (int64_t) ((asm_imm *) mov_inst.src)->value;
      if (imm > INT32_MAX) {
        return mov_imm64_to_reg64(mov_inst);
      }

      return mov_imm32_to_reg64(mov_inst);
    }
  }

  error_exit(0, "can not parser mov type");
}

/**
 * amd64 结构体转 2 进制汇编
 * @param inst
 * @return
 */
elf_text_item asm_inst_lower(asm_inst inst) {
  switch (inst.op) {
    case ASM_OP_TYPE_MOV: return inst_mov_lower(inst);
    default:exit(0);
  }
}
