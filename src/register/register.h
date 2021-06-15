#ifndef NATURE_SRC_REGISTER_REGISTER_H_
#define NATURE_SRC_REGISTER_REGISTER_H_
#include "src/value.h"

typedef struct {
  string type;
  string ident;
  uint8_t id; // 唯一编号
} reg;

typedef struct {
  uint8_t count;
  reg *list[UINT8_MAX];
} regs; // 指定架构的物理寄存器列表

// TODO 根据指定系统填充 physical_regs
regs physical_regs;

#endif //NATURE_SRC_REGISTER_REGISTER_H_
