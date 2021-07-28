#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "lir.h"

lir_operand *lir_new_memory_operand(lir_operand *base, size_t offset, size_t length) {
  lir_operand_memory *memory_operand = malloc(sizeof(lir_operand_memory));
  memory_operand->base = base;
  // 根据 index + 类型计算偏移量
  memory_operand->offset = offset;

  lir_operand *operand = NEW(lir_operand);
  operand->type = LIR_OPERAND_TYPE_MEMORY;
  operand->value = memory_operand;
  return operand;
}

lir_op *lir_runtime_call(char *name, lir_operand *result, int arg_count, ...) {
  lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
  params_operand->count = 0;

  va_list args;
  va_start(args, arg_count); // 初始化参数
  for (int i = 0; i < arg_count; ++i) {
    lir_operand *param = va_arg(args, lir_operand*);
    params_operand->list[params_operand->count++] = param;
  }
  va_end(args);
  lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);
  return lir_new_op(LIR_OP_TYPE_RUNTIME_CALL, lir_new_label_operand(name), call_params_operand, result);
}

lir_operand *lir_new_var_operand(char *ident) {
  lir_operand *operand = NEW(lir_operand);
  operand->type = LIR_OPERAND_TYPE_VAR;
  operand->value = LIR_NEW_VAR_OPERAND(ident);
  return operand;
}

lir_operand *lir_new_temp_var_operand() {
  char *temp_name = malloc(strlen(TEMP_IDENT) + sizeof(int) + 2);
  sprintf(temp_name, "%d_%s", lir_unique_count++, TEMP_IDENT);
  return lir_new_var_operand(temp_name);
}

lir_operand *lir_new_label_operand(char *ident) {
  lir_operand *operand = NEW(lir_operand);
  operand->type = LIR_OPERAND_TYPE_LABEL;
  operand->value = LIR_NEW_LABEL_OPERAND(ident);
  return operand;
}

lir_op *lir_op_label(char *ident) {
  lir_op *op = NEW(lir_op);
  op->type = LIR_OP_TYPE_LABEL;
  op->result = lir_new_label_operand(ident);
  return op;
}

lir_op *lir_op_unique_label(char *ident) {
  lir_op *op = NEW(lir_op);
  op->type = LIR_OP_TYPE_LABEL;
  op->result = lir_new_label_operand(LIR_UNIQUE_NAME(ident));
  return op;
}

lir_op *lir_op_goto(lir_operand *label) {
  lir_op *op = NEW(lir_op);
  op->type = LIR_OP_TYPE_LABEL;
  op->result = label;
  return op;
}

lir_op *lir_op_move(lir_operand *dst, lir_operand *src) {
  lir_op *op = NEW(lir_op);
  op->type = LIR_OP_TYPE_MOVE;
  op->first = src;
  op->result = dst;
  return op;
}

lir_op *lir_new_op(lir_op_type type, lir_operand *first, lir_operand *second, lir_operand *result) {
  lir_op *op = NEW(lir_op);
  op->type = type;
  op->first = first;
  op->second = second;
  op->result = result;
  return op;
}
