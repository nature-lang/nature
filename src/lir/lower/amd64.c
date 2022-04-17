#include "amd64.h"
#include "src/type.h"
#include "src/assembler/amd64/register.h"
#include "src/lib/error.h"

amd64_lower_fn amd64_lower_table[] = {
    [LIR_OP_TYPE_ADD] = amd64_lower_add,
};

/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
asm_inst_t *amd64_lower_add(lir_op op, uint8_t *count) {
  if (op.type == TYPE_INT) {
    *count = 100; // 指令类型不太确定，所以指令长度也不是特别的确定
    asm_inst_t *insts = malloc(sizeof(asm_inst_t) * *count);
    // lir operand to asm operand
//    insts[0] = ASM_INST("mov", op.first,)

    // 复合 operand 如何处理？复合操作预处理
    // if is complex => pre handle


    return insts;
  }
  return NULL;
}

asm_operand_t *amd64_lir_to_operand(lir_operand *operand) {
  if (operand->type == LIR_OPERAND_TYPE_VAR) {
    lir_operand_var *v = operand->value;
    if (v->stack_frame_offset > 0) {
      return DISP_REG(rbp, v->stack_frame_offset);
    }
    if (v->reg_id > 0) {
      asm_operand_register_t *reg = (asm_operand_register_t *) physical_regs.list[v->reg_id];
      return REG(reg);
    }
    error_exit("[amd64_lir_to_operand] type var not a reg or stack_frame_offset");
  }

  // 立即数
  if (operand->type == LIR_OPERAND_TYPE_IMMEDIATE) {
    lir_operand_immediate *v = operand->value;
    if (v->type == TYPE_INT) {
      return UINT64(v->int_value);
    }
    if (v->type == TYPE_FLOAT) {
      return FLOAT32(v->float_value);
    }
    if (v->type == TYPE_BOOL) {
      return UINT8(v->bool_value);
    }
    error_exit("[amd64_lir_to_operand] type immediate not expected");
  }

  // label(都是局部 label)
  if (operand->type == LIR_OPERAND_TYPE_LABEL) {
    lir_operand_label *v = operand->value;
    return SYMBOL(v->ident, true, true);
  }

//  // sib mem = mov -> reg + offset
//  if (operand->type == LIR_OPERAND_TYPE_MEMORY) {
//    lir_operand_memory *v = operand->value;
//  }

  error_exit("[amd64_lir_to_operand] operand type not ident");
  return NULL;
}
