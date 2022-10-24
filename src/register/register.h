#ifndef NATURE_SRC_REGISTER_REGISTER_H_
#define NATURE_SRC_REGISTER_REGISTER_H_

#include "src/value.h"
#include "utils/slice.h"
#include "utils/table.h"
#include "src/build/config.h"
#include "src/type.h"

table_t *reg_table; // 根据 index 和 size 定位具体的寄存器
slice_t *regs;

typedef enum {
    REG_TYPE_INT,
    REG_TYPE_FLOAT,
} reg_type_e;

typedef struct {
    string name;
    uint8_t index; // index 对应 intel 手册表中的索引，可以直接编译进 modrm 中
    uint8_t size; // 实际的位宽, 对应 intel 手册
    reg_type_e type; // 寄存器类型, float and int
    uint8_t alloc_id; // 寄存器分配期间的 id，能通过 id 进行唯一检索
} reg_t; // 做类型转换

reg_t *alloc_regs[UINT8_MAX];

uint8_t alloc_reg_count();

string reg_table_key(uint8_t index, uint8_t size);

/**
 * 导向具体架构的 reg_select
 * @param index
 * @param base
 * @return
 */
reg_t *reg_select(uint8_t index, type_base_t base);

reg_t *reg_find(uint8_t index, size_t size);

reg_t *covert_alloc_reg(reg_t *reg);

/**
 * 注册相应架构的寄存器到 reg_table 和 regs 中
 */
void reg_init();

reg_t *reg_new(char *name, uint8_t index, reg_type_e type, uint8_t size, uint8_t reg_id);

reg_type_e type_base_trans(type_base_t t);

#endif //NATURE_SRC_REGISTER_REGISTER_H_
