#ifndef NATURE_SRC_REGISTER_REGISTER_H_
#define NATURE_SRC_REGISTER_REGISTER_H_

#include "src/value.h"

typedef struct {
    string name;
    uint8_t index; // index 对应 intel 手册表中的索引，可以直接编译进 modrm 中
    uint8_t size;
    uint8_t id; // 在 physical register 中的 index
} reg_t; // 做类型转换

typedef struct {
    uint8_t count;
    reg_t *list[UINT8_MAX];
} regs_t; // 指定架构的物理寄存器列表

regs_t physical_regs;
// TODO 根据指定系统填充 physical_regs
// TODO 如何处理共用空间比如 rax 和 eax 就共用内存空间，其 id 应该是相同的！
// 导向具体架构的 register_find
regs_t *register_find(uint8_t index, uint8_t size);

#endif //NATURE_SRC_REGISTER_REGISTER_H_
