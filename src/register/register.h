#ifndef NATURE_SRC_REGISTER_REGISTER_H_
#define NATURE_SRC_REGISTER_REGISTER_H_
#include "src/value.h"

typedef struct {
  string name;
  uint8_t id; // 唯一编码, 可以是 regs 中的 index
} reg;

typedef struct {
  uint8_t count;
  reg *list[UINT8_MAX];
} regs_t; // 指定架构的物理寄存器列表

// TODO 根据指定系统填充 physical_regs
regs_t physical_regs;

#endif //NATURE_SRC_REGISTER_REGISTER_H_
