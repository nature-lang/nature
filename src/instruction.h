#ifndef NATURE_SRC_INSTRUCTION_H_
#define NATURE_SRC_INSTRUCTION_H_
#include "value.h"

#define AS_INST(object, type) ((inst){type, (void *)object})
#define NEW_INST(inst_type) ((inst_type *) malloc(sizeof(inst_type)))

enum {
  INST_MOV,
  INST_ADD,
  INST_SUB,
  INST_MUL,
  INST_DIV,
  INST_JMP,
  INST_CMP,
  INST_LABEL,
} inst_type;

enum {
  INST_JMP_TYPE_EQUAL,
  INST_JMP_TYPE_NOT_EQUAL
} inst_jmp_type;

//inst as_inst(void *i, string type) {
//  inst inst;
//  inst.type = type;
//  inst.ins = i;
//  return inst;
//}

typedef struct {
  int8_t byte; // 字节数
  string type; // 数据类型
  string src; // 多种类型、寄存器、立即数、内存地址
  string dst;
} inst_mov, inst_add, inst_sub, inst_mul, inst_div;

typedef struct {
  int8_t byte; // 字节数
  string left; // 多种类型、寄存器、立即数、内存地址
  string right;
} inst_cmp;

typedef struct {
  string dst; // custom is label name
  int type; // nz/z/ne/z
} inst_jmp;

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
//void instructions_init(instructions *ins);
void inst_insert(insts *insts, void *inst, int8_t type);
insts *inst_append(insts *dts, insts *src);


// 需要用到的 register
// 内存相关操作
// 需要用到的指令
// 虚拟指令
// 指令组合(平面组合),组合的起点是 main blocks
// 指令集不直接丢进指令序列而是通过组合的形式生成


// 块指令提交
// 在某个指令之前添加指令
#endif //NATURE_SRC_INSTRUCTION_H_
