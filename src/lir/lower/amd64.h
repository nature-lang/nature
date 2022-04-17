//
// Created by weiwenhao on 2022/4/17.
//

#ifndef NATURE_SRC_LIR_LOWER_AMD64_H_
#define NATURE_SRC_LIR_LOWER_AMD64_H_

#include "src/assembler/amd64/asm.h"
#include "src/lir/lir.h"
#include "src/register/register.h"

typedef struct {
  asm_inst_t *inst_list;
  uint8_t inst_count;
  // fixed reg(需要一个更加朴素的保存方式)
  regs_t fixed_regs;
} amd64_lower_result_t;

void amd64_lower(lir_op op);

#endif //NATURE_SRC_LIR_LOWER_AMD64_H_
