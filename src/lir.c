#include <stdarg.h>
#include "lir.h"

lir_operand *lir_new_memory_operand(lir_operand *base, size_t offset, size_t length) {
  lir_operand_memory *memory_operand = malloc(sizeof(lir_operand_memory));
  memory_operand->base = base;
  // 根据 index + 类型计算偏移量
  memory_operand->offset = offset;

  lir_operand *operand = malloc(sizeof(lir_operand));
  operand->type = LIR_OPERAND_TYPE_MEMORY;
  operand->value = memory_operand;
  return operand;
}

lir_op *lir_runtime_call(char *name, lir_operand *result, int arg_count, ...) {
  lir_op *call_op = lir_op_new(LIR_OP_TYPE_RUNTIME_CALL);
  call_op->first = lir_op_label(name)->first; // 函数名称
  call_op->result = *result;

  lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
  params_operand->count = 0;

  va_list args;
  va_start(args, arg_count); // 初始化参数
  for (int i = 0; i < arg_count; ++i) {
    lir_operand *param = va_arg(args, lir_operand*);
    params_operand->list[params_operand->count++] = param;
  }
  va_end(args);
  lir_operand call_params_operand = {
      .type= LIR_OPERAND_TYPE_ACTUAL_PARAM,
      .value = params_operand};

  call_op->second = call_params_operand;

  return call_op;
}