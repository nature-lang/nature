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

/**
 * 调用方式和 call 没有什么区别。关键是怎么调过去？
 * 假如我自定义有一个 c 函数叫 print,直接在这里用 if else 调用过去？
 * 不行，最终需要的是生成的 .a 运行时，print 函数需要在 .a 目标文件中才行。
 * @param c
 * @param op
 * @param count
 * @return
 */
asm_inst_t **amd64_lower_builtin_call(closure *c, lir_op op, uint8_t *count);

asm_inst_t **amd64_lower(closure *c, lir_op op, uint8_t *count);

asm_inst_t **amd64_lower_return(closure *c, lir_op op, uint8_t *count);

asm_inst_t **amd64_lower_add(closure *c, lir_op op, uint8_t *count);

asm_inst_t **amd64_lower_mov(closure *c, lir_op op, uint8_t *count);

/**
 * lir 中的简单操作数转换成 asm 中的操作数
 * @param operand
 * @return
 */
asm_operand_t *amd64_lower_to_asm_operand(lir_operand *operand);

bool amd64_lower_is_complex_operand(lir_operand *operand);

reg_t *amd64_lower_next_reg(regs_t *used, uint8_t type);

/**
 * 返回下一个可用参数，可能是寄存器参数也可能是内存偏移
 * @param count
 * @param asm_operand
 * @return
 */
asm_operand_t *amd64_lower_next_actual_target(uint8_t *count);

// 只要返回了指令就有一个使用的寄存器的列表，已经使用的固定寄存器就不能重复使用
asm_insts_t amd64_lower_complex_to_asm_operand(lir_operand *operand,
                                               asm_operand_t *asm_operand,
                                               regs_t *used_regs);

#endif //NATURE_SRC_LIR_LOWER_AMD64_H_
