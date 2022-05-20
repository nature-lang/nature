//
// Created by weiwenhao on 2022/4/17.
//

#ifndef NATURE_SRC_LIR_LOWER_AMD64_H_
#define NATURE_SRC_LIR_LOWER_AMD64_H_

#include "src/assembler/amd64/asm.h"
#include "src/lir/lir.h"
#include "src/register/register.h"

typedef struct {
  asm_inst_t **inst_list;
  uint8_t inst_count;
//  regs_t used_regs; // 使用的寄存器列表
} amd64_lower_result_t;

typedef asm_inst_t **(*amd64_lower_fn)(closure *c, lir_op op, uint8_t *count);

asm_inst_t **amd64_lower(closure *c, lir_op op, uint8_t *count);

asm_inst_t **amd64_lower_return(closure *c, lir_op op, uint8_t *count);

asm_inst_t **amd64_lower_add(closure *c, lir_op op, uint8_t *count);

asm_inst_t **amd64_lower_mov(closure *c, lir_op op, uint8_t *count);

/**
 * lir 中的简单操作数转换成 asm 中的操作数
 * @param operand
 * @return
 */
asm_operand_t *amd64_lower_simple_lir_to_asm_operand(lir_operand *operand);

bool amd64_lower_is_complex_operand(lir_operand *operand);

reg_t *amd64_lower_next_reg(regs_t *used, uint8_t size);

// 只要返回了指令就有一个使用的寄存器的列表，已经使用的固定寄存器就不能重复使用
amd64_lower_result_t amd64_lower_lir_to_asm_operand(lir_operand *operand,
                                                    asm_operand_t *asm_operand,
                                                    regs_t *used_regs);

#endif //NATURE_SRC_LIR_LOWER_AMD64_H_
