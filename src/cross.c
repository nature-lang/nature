#include "cross.h"

table_t *reg_table; // 根据 index 和 size 定位具体的寄存器
slice_t *regs;
reg_t *alloc_regs[UINT8_MAX];
