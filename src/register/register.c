#include "register.h"
#include "src/cross.h"
#include "utils/helper.h"
#include "arch/amd64.h"
#include <assert.h>

table_t *reg_table; // 根据 index 和 size 定位具体的寄存器
slice_t *regs;
reg_t *alloc_regs[UINT8_MAX];

char *reg_table_key(lir_flag_t alloc_type, uint8_t index, uint8_t size) {
    int64_t int_key = (int64_t) index << 8;
    int64_t alloc_type_flag = (int64_t) alloc_type << 16;
    int_key |= alloc_type_flag;
    int_key |= size;
    return itoa(int_key);
}

reg_t *reg_find(lir_flag_t alloc_type, uint8_t index, size_t size) {
    return table_get(reg_table, reg_table_key(alloc_type, index, size));
}

reg_t *reg_new(char *name, uint8_t index, lir_flag_t alloc_type, uint8_t size, uint8_t reg_id) {
    reg_t *reg = NEW(reg_t);
    reg->name = name;
    reg->index = index;
    reg->flag |= FLAG(alloc_type);
    reg->size = size;
    reg->alloc_id = reg_id; // 0 表示未分配

    if (reg_id > 0) {
        alloc_regs[reg_id] = reg;
    }

    table_set(reg_table, reg_table_key(alloc_type, index, size), reg);
    slice_push(regs, reg);
    return reg;
}

lir_flag_t type_kind_trans_alloc(type_kind kind) {
    if (is_float(kind)) {
        return LIR_FLAG_ALLOC_FLOAT;
    }

    return LIR_FLAG_ALLOC_INT;
}

reg_t *covert_alloc_reg(reg_t *reg) {
    if (reg->flag & FLAG(LIR_FLAG_ALLOC_FLOAT)) {
        return reg_find(LIR_FLAG_ALLOC_FLOAT, reg->index, OWORD);
    }

    return reg_find(LIR_FLAG_ALLOC_INT, reg->index, QWORD);
}

