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
    // operand 改写
    // 复合 operand 如何处理？复合操作预处理
    // if is complex => pre handle => asm_inst_t + asm_operand_t


    return insts;
  }
  return NULL;
}

asm_operand_t *amd64_lir_to_asm_operand(lir_operand *operand) {
  if (operand->type == LIR_OPERAND_TYPE_VAR) {
    lir_operand_var *v = operand->value;
    if (v->stack_frame_offset > 0) {
      return DISP_REG(rbp, v->stack_frame_offset);
    }
    if (v->reg_id > 0) {
      asm_operand_register_t *reg = (asm_operand_register_t *) physical_regs.list[v->reg_id];
      return REG(reg);
    }
    error_exit("[amd64_lir_to_asm_operand] type var not a reg or stack_frame_offset");
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
    error_exit("[amd64_lir_to_asm_operand] type immediate not expected");
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

  error_exit("[amd64_lir_to_asm_operand] operand type not ident");
  return NULL;
}

amd64_lower_result_t amd64_complex_lir_to_asm_operand(lir_operand *operand, asm_operand_t *asm_operand) {
  amd64_lower_result_t result = {
      .inst_count = 0,
      .inst_list = NULL,
      .fixed_regs.count = 0,
      .fixed_regs.list = {NULL},
  };

  if (operand->type == LIR_OPERAND_TYPE_MEMORY) {
    lir_operand_memory *v = operand->value;
    // base 类型必须为 var
    if (v->base->type != LIR_OPERAND_TYPE_VAR) {
      error_exit("[amd64_complex_lir_to_asm_operand] operand type memory, but that base not type var");
    }

    lir_operand_var *var = v->base->value;
    // 如果是寄存器类型就直接返回 disp reg operand
    if (var->reg_id > 0) {
      asm_operand_register_t *reg = (asm_operand_register_t *) physical_regs.list[var->reg_id];
      asm_operand_t *temp = DISP_REG(reg, v->offset);
      ASM_OPERAND_COPY(asm_operand, temp);
      free(temp);
      return result;
    }

    if (var->stack_frame_offset > 0) {
      // 需要占用一个临时寄存器(选定 rax 即可)
      // 生成 mov 指令（asm_mov）
      result.inst_count = 1;
      result.inst_list = malloc(sizeof(asm_inst_t) * 1);
      result.inst_list[0] = ASM_INST("mov", { REG(rax), DISP_REG(rbp, var->stack_frame_offset) });

      asm_operand_t *temp = REG(rax);
      ASM_OPERAND_COPY(asm_operand, temp);
      return result;
    }

    error_exit("[amd64_complex_lir_to_asm_operand]  var cannot reg_id or stack_frame_offset");
  }

  // TODO LIR_OPERAND_TYPE_ACTUAL_PARAM/LIR_OPERAND_TYPE_PHI_BODY

}
