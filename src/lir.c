#include "lir.h"

lir_operand *lir_new_memory_operand(lir_operand_var *base, int64_t offset) {
  lir_operand_memory *memory_operand = malloc(sizeof(lir_operand_memory));
  memory_operand->base = base;
  // 根据 index + 类型计算偏移量
  memory_operand->offset = offset;

  lir_operand *operand = malloc(sizeof(lir_operand));
  operand->type = LIR_OPERAND_TYPE_MEMORY;
  operand->value = memory_operand;
  return operand;
}
