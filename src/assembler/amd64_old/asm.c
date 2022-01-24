#include "asm.h"
#include "mov.h"
#include "add.h"
#include "sub.h"
#include "mul.h"
#include "div.h"
#include "set.h"
#include "jmp.h"
#include "jcc.h"

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

/**
 * amd64 结构体转 2 进制汇编
 * @param inst
 * @return
 */
elf_text_item asm_inst_lower(asm_inst inst) {
  switch (inst.op) {
    case ASM_OP_TYPE_MOV: return asm_inst_mov_lower(inst);
    case ASM_OP_TYPE_ADD: return asm_inst_add_lower(inst);
    case ASM_OP_TYPE_SUB: return asm_inst_sub_lower(inst);
    case ASM_OP_TYPE_MUL: return asm_inst_mul_lower(inst);
    case ASM_OP_TYPE_DIV: return asm_inst_div_lower(inst);
    case ASM_OP_TYPE_JMP: return asm_inst_jmp_lower(inst);
    case ASM_OP_TYPE_JE: return asm_inst_jcc_lower(inst);
    case ASM_OP_TYPE_SETG:
    case ASM_OP_TYPE_SETGE:
    case ASM_OP_TYPE_SETE:
    case ASM_OP_TYPE_SETNE:
    case ASM_OP_TYPE_SETL:
    case ASM_OP_TYPE_SETLE:return asm_inst_set_lower(inst);
    default:error_exit(0, "cannot parser asm_ope_type");
  }
}
