#include "asm.h"

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

static elf_text_item asm_inst_mov_lower(asm_inst mov_inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  // 根据 src 进行的区别进行处理
  if (mov_inst.size == 64) {
    
  }
}

elf_text_item asm_inst_lower(asm_inst inst) {

  switch (inst.op) {
    case ASM_OP_TYPE_MOV: return asm_inst_mov_lower(inst);
    default:exit(0);
  }
}
