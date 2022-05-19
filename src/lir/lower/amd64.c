#include "amd64.h"
#include "src/type.h"
#include "src/assembler/amd64/register.h"
#include "src/lib/error.h"
#include <math.h>

amd64_lower_fn amd64_lower_table[] = {
    [LIR_OP_TYPE_ADD] = amd64_lower_add,
};

/**
 * LIR_OP_TYPE_CALL
 * @param op
 * @param count
 * @return
 */
asm_inst_t **amd64_lower_call(closure *c, lir_op op, uint8_t *count) {
  // 特殊逻辑，如果响应的参数是一个结构体，就需要做隐藏参数的处理
}

/**
 * 核心问题是: 在结构体作为返回值时，当外部调用将函数的返回地址作为参数 rdi 传递给函数时，
 * 根据 ABI 规定，函数操作的第一步就是对 rdi 入栈，但是当 lower return 时,我并不知道 rdi 被存储在了栈的什么位置？
 * 但是实际上是能够知道的，包括初始化时 sub rbp,n 的 n 也是可以在寄存器分配阶段就确定下来的。
 * n 的信息作为 closure 的属性存储在 closure 中，如何将相关信息传递给 lower ?, 参数 1 改成 closure?
 * 在结构体中 temp_var 存储的是结构体的起始地址。不能直接 return 起始地址，大数据会随着函数栈帧消亡。 而是将结构体作为整个值传递。
 * 既然已经知道了结构体的起始位置，以及隐藏参数的所在栈帧。 就可以直接进行结构体返回值的构建。
 * @param c
 * @param ast
 * @return
 */
asm_inst_t **amd64_lower_return(closure *c, lir_op op, uint8_t *count) {
}

/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
asm_inst_t **amd64_lower_add(closure *c, lir_op op, uint8_t *count) {
  if (op.data_type == TYPE_INT) {
    *count = 0; // 指令类型不太确定，所以指令长度也不是特别的确定
    asm_inst_t **insts = malloc(sizeof(asm_inst_t *) * 100);
    regs_t used_regs = {.count = 0};

    // 参数转换
    asm_operand_t *first = NEW(asm_operand_t);
    amd64_lower_result_t temp = amd64_lower_lir_to_asm_operand(op.first, first, &used_regs);
    if (temp.inst_count > 0) {
      for (int i = 0; i < temp.inst_count; ++i) {
        insts[(*count)++] = temp.inst_list[i];
      }
    }
    asm_operand_t *second = NEW(asm_operand_t);
    temp = amd64_lower_lir_to_asm_operand(op.second, second, &used_regs);
    if (temp.inst_count > 0) {
      for (int i = 0; i < temp.inst_count; ++i) {
        insts[(*count)++] = temp.inst_list[i];
      }
    }
    asm_operand_t *result = NEW(asm_operand_t);
    temp = amd64_lower_lir_to_asm_operand(op.result, result, &used_regs);
    if (temp.inst_count > 0) {
      for (int i = 0; i < temp.inst_count; ++i) {
        insts[(*count)++] = temp.inst_list[i];
      }
    }

    reg_t *reg = amd64_lower_next_reg(&used_regs, op.size);
    insts[(*count)++] = ASM_INST("mov", { REG(reg), first });
    insts[(*count)++] = ASM_INST("add", { REG(reg), second });
    insts[(*count)++] = ASM_INST("mov", { result, REG(reg) });

    realloc(insts, *count);
    return insts;
  }
  return NULL;
}

asm_inst_t **amd64_lower_mov(closure *c, lir_op op, uint8_t *count) {
  if (op.data_type == TYPE_INT) {
    *count = 0; // 指令类型不太确定，所以指令长度也不是特别的确定
    asm_inst_t **insts = malloc(sizeof(asm_inst_t *) * 100);
    regs_t used_regs = {.count = 0};

    // 参数转换
    asm_operand_t *first = NEW(asm_operand_t);
    amd64_lower_result_t temp = amd64_lower_lir_to_asm_operand(op.first, first, &used_regs);
    if (temp.inst_count > 0) {
      for (int i = 0; i < temp.inst_count; ++i) {
        insts[(*count)++] = temp.inst_list[i];
      }
    }

    asm_operand_t *result = NEW(asm_operand_t);
    temp = amd64_lower_lir_to_asm_operand(op.result, result, &used_regs);
    if (temp.inst_count > 0) {
      for (int i = 0; i < temp.inst_count; ++i) {
        insts[(*count)++] = temp.inst_list[i];
      }
    }

    reg_t *reg = amd64_lower_next_reg(&used_regs, op.size);
    insts[(*count)++] = ASM_INST("mov", { REG(reg), first });
    insts[(*count)++] = ASM_INST("mov", { result, REG(reg) });

    realloc(insts, *count);
    return insts;
  }

  // 结构体处理
  if (op.data_type == TYPE_STRUCT) {
    *count = 0;
    asm_inst_t **insts = malloc(sizeof(asm_inst_t *) * 100);
//    regs_t used_regs = {.count = 0};
    // first => result
    // 如果操作数是内存地址，则直接 lea, 如果操作数是寄存器，则不用操作
    // lea first to rax
    asm_operand_t *first_reg = REG(rax);
    asm_operand_t *first = amd64_lower_simple_lir_to_asm_operand(op.first);
    if (first->type == ASM_OPERAND_TYPE_DISP_REGISTER) {
      insts[(*count)++] = ASM_INST("lea", { REG(rax), first });
    } else {
      first_reg = first;
    }
    // lea result to rdx
    asm_operand_t *result_reg = REG(rdx);
    asm_operand_t *result = amd64_lower_simple_lir_to_asm_operand(op.result);
    if (result->type == ASM_OPERAND_TYPE_DISP_REGISTER) {
      insts[(*count)++] = ASM_INST("lea", { REG(rdx), result });
    } else {
      result_reg = result;
    }

    // mov rax,rsi, mov rdx,rdi, mov count,rcx
    int rep_count = ceil((float) op.size / QWORD);
    insts[(*count)++] = ASM_INST("mov", { REG(rsi), first_reg });
    insts[(*count)++] = ASM_INST("mov", { REG(rdi), result_reg });
    insts[(*count)++] = ASM_INST("mov", { REG(rcx), UINT64(rep_count) });
    insts[(*count)++] = MOVSQ(0xF3);
    realloc(insts, *count);
    return insts;
  }
}

asm_operand_t *amd64_lower_simple_lir_to_asm_operand(lir_operand *operand) {
  if (operand->type == LIR_OPERAND_TYPE_VAR) {
    lir_operand_var *v = operand->value;
    if (v->stack_frame_offset > 0) {
      return DISP_REG(rbp, v->stack_frame_offset);
    }
    if (v->reg_id > 0) {
      asm_operand_register_t *reg = (asm_operand_register_t *) physical_regs.list[v->reg_id];
      return REG(reg);
    }
    error_exit("[amd64_lower_simple_lir_to_asm_operand] type var not a reg or stack_frame_offset");
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
    error_exit("[amd64_lower_simple_lir_to_asm_operand] type immediate not expected");
  }

  // label(都是局部 label)
  if (operand->type == LIR_OPERAND_TYPE_LABEL) {
    lir_operand_label *v = operand->value;
    return SYMBOL(v->ident, true, true);
  }

  error_exit("[amd64_lower_simple_lir_to_asm_operand] operand type not ident");
  return NULL;
}

amd64_lower_result_t amd64_lower_lir_to_asm_operand(lir_operand *operand,
                                                    asm_operand_t *asm_operand,
                                                    regs_t *used_regs) {
  amd64_lower_result_t result = {
      .inst_count = 0,
      .inst_list = NULL,
  };

  if (operand->type == LIR_OPERAND_TYPE_MEMORY) {
    lir_operand_memory *v = operand->value;
    // base 类型必须为 var
    if (v->base->type != LIR_OPERAND_TYPE_VAR) {
      error_exit("[amd64_lir_to_asm_operand] operand type memory, but that base not type var");
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
      // 需要占用一个临时寄存器
      reg_t *reg = amd64_lower_next_reg(used_regs, QWORD);

      // 生成 mov 指令（asm_mov）
      result.inst_count = 2;
      result.inst_list = malloc(sizeof(asm_inst_t) * result.inst_count);
      result.inst_list[0] = ASM_INST("mov", { REG(reg), DISP_REG(rbp, var->stack_frame_offset) });

      asm_operand_t *temp = DISP_REG(reg, v->offset);
      ASM_OPERAND_COPY(asm_operand, temp);
      return result;
    }

    error_exit("[amd64_lir_to_asm_operand]  var cannot reg_id or stack_frame_offset");
  }

  // TODO LIR_OPERAND_TYPE_ACTUAL_PARAM

}
