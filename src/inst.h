#ifndef NATURE_SRC_INSTRUCTION_H_
#define NATURE_SRC_INSTRUCTION_H_
#include "value.h"

#define AS_INST(object, type) ((inst){type, (void *)object})
#define NEW_INST(inst_type) ((inst_type *) malloc(sizeof(inst_type)))
#define AS_INST_OPERAND(object, type) ((inst_operand){type, (void *)object})

#define REG_MAX 10

enum {
  INST_OPERAND_TYPE_REG,
  INST_OPERAND_TYPE_VAR,
  INST_OPERAND_TYPE_OBJ_PROPERTY,
  INST_OPERAND_TYPE_LITERAL,
  INST_OPERAND_TYPE_LABEL,
  INST_OPERAND_TYPE_POINT,
} inst_operand_type;

enum {
  INST_LOAD,
  INST_ADD,
  INST_SUB,
  INST_MUL,
  INST_DIV,
  INST_JMP,
  INST_COMPARE_GOTO,
  INST_LABEL,
} inst_type;

enum {
  INST_JMP_TYPE_EQUAL,
  INST_JMP_TYPE_NOT_EQUAL
} inst_jmp_type;

typedef ast_obj_property inst_operand_obj_property;
typedef ast_ident inst_operand_var;
typedef uint64_t inst_operand_point;
typedef string inst_operand_reg;
typedef string inst_operand_label;
typedef ast_literal inst_operand_literal;

// 操作对象
typedef struct {
  int8_t type; // 寄存器、变量、变量之~属性访问
  void *value;
} inst_operand;

typedef struct {
  inst_operand src; // 多种类型、寄存器、立即数、内存地址
  inst_operand dst;
} inst_load, inst_add, inst_sub, inst_mul, inst_div;

typedef struct {
  inst_operand label;
} inst_goto;

typedef struct {
  inst_operand actual_params[UINT8_MAX];
  uint8_t actual_param_count;
  inst_operand label;
} inst_call;

typedef struct {
  inst_operand expect;
  inst_operand actual;
  inst_operand label; // 相等时跳转
} inst_compare_goto;

typedef struct {
  string name;
} inst_label;

typedef struct {
  string type;
  void *ins;
} inst;

// 自增指令序列
typedef struct {
  size_t count;
  size_t capacity;
  inst *list;
} insts;

insts *inst_new();
void inst_insert(insts *insts, void *inst, int8_t type);
insts *inst_append(insts *dts, insts *src);
insts *inst_preserving(insts *dts, insts *src, inst_operand_reg saves[REG_MAX]);

inst_operand_reg reg_temp();
inst_operand_reg reg_val();
inst_operand_reg reg_env();

// 需要用到的 register
// 内存相关操作
// 需要用到的指令
// 虚拟指令
// 指令组合(平面组合),组合的起点是 main blocks
// 指令集不直接丢进指令序列而是通过组合的形式生成


// 块指令提交
// 在某个指令之前添加指令
#endif //NATURE_SRC_INSTRUCTION_H_