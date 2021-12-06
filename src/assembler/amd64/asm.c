#include "asm.h"
#include "mov.h"
#include "add.h"

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
    default:exit(0);
  }
}
