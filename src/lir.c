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

lir_op *lir_runtime_two_param_call(string name, lir_operand result, lir_operand *first, lir_operand *second) {
  lir_op *call_op = lir_new_op(LIR_OP_TYPE_RUNTIME_CALL);
  call_op->first = lir_label(name)->first; // 函数名称
  lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
  params_operand->count = 0;
  params_operand->list[params_operand->count++] = first;
  params_operand->list[params_operand->count++] = second;
  lir_operand call_params_operand = {
      .type= LIR_OPERAND_TYPE_ACTUAL_PARAM,
      .value = params_operand};
  call_op->second = call_params_operand;
  call_op->result = result;
  return call_op;
}
