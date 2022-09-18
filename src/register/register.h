#ifndef NATURE_SRC_REGISTER_REGISTER_H_
#define NATURE_SRC_REGISTER_REGISTER_H_

#include "src/value.h"
#include "utils/slice.h"
#include "utils/table.h"
#include "src/build/config.h"

table *reg_table; // 根据 index 和 size 定位具体的寄存器
slice_t *regs;

typedef struct {
    string name;
    uint8_t index; // index 对应 intel 手册表中的索引，可以直接编译进 modrm 中
    uint8_t size;
//    uint8_t id; // 在 physical register 中的 index
} reg_t; // 做类型转换

/**
 * 导向具体架构的 register_find
 * @param index
 * @param size
 * @return
 */
reg_t *register_find(uint8_t index, uint8_t size);

/**
 * 注册相应架构的寄存器到 reg_table 和 regs 中
 */
void register_init();

reg_t *reg_new(char *name, uint8_t index, uint8_t size);

#endif //NATURE_SRC_REGISTER_REGISTER_H_
