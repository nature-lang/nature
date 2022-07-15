#ifndef NATURE_SRC_LIR_LOWER_AMD64_H_
#define NATURE_SRC_LIR_LOWER_AMD64_H_

#include "src/assembler/amd64/asm.h"
#include "src/lir/lir.h"
#include "src/register/register.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef list *(*amd64_lower_fn)(closure *c, lir_op *op);

list *amd64_decl_list;

int amd64_decl_unique_count;

#define ADM64_DECL_PREFIX "v"


#define AMD64_DECL_UNIQUE_NAME() \
({                                 \
   char *temp_name = malloc(strlen(ADM64_DECL_PREFIX) + sizeof(int) + 2); \
   sprintf(temp_name, "%s_%d", ADM64_DECL_PREFIX, amd64_decl_unique_count++); \
   temp_name;                                   \
})


void amd64_lower_init();

/**
 * @param c
 * @return
 */
list *amd64_lower_fn_begin(closure *c);

list *amd64_lower_fn_formal_params(closure *c);

list *amd64_lower_fn_end(closure *c);

list *amd64_lower_closure(closure *c);

/**
 * 分发入口, 基于 op->op 做选择(包含 label op)
 * @param c
 * @param op
 * @return
 */
list *amd64_lower_op(closure *c, lir_op *op);

list *amd64_lower_label(closure *c, lir_op *op);

list *amd64_lower_call(closure *c, lir_op *op);

list *amd64_lower_return(closure *c, lir_op *op);

list *amd64_lower_add(closure *c, lir_op *op);

list *amd64_lower_mov(closure *c, lir_op *op);

/**
 * lir 中的简单操作数转换成 asm 中的操作数
 * @param operand
 * @return
 */
asm_operand_t *amd64_lower_to_asm_operand(lir_operand *operand);

// 返回下一个可用的寄存器(属于一条指令的 fixed_reg)
reg_t *amd64_lower_next_reg(regs_t *used, uint8_t size);

/**
 * 返回下一个可用参数，可能是寄存器参数也可能是内存偏移
 * @param count
 * @param asm_operand
 * @return
 */
reg_t *amd64_lower_next_actual_reg_target(uint8_t used[7], uint8_t size);

// 只要返回了指令就有一个使用的寄存器的列表，已经使用的固定寄存器就不能重复使用
list *amd64_lower_complex_to_asm_operand(lir_operand *operand,
                                         asm_operand_t *asm_operand,
                                         regs_t *used_regs);


#endif //NATURE_SRC_LIR_LOWER_AMD64_H_
