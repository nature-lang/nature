#include "register.h"
#include "src/cross.h"
#include "utils/helper.h"
#include "amd64.h"
#include <assert.h>

string reg_table_key(uint8_t index, uint8_t size) {
    uint16_t int_key = ((uint16_t) index << 8) | size;
    return itoa(int_key);
}

reg_t *reg_find(uint8_t index, size_t size) {
    return table_get(reg_table, reg_table_key(index, size));
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

    table_set(reg_table, reg_table_key(index, size), reg);
    slice_push(regs, reg);
    return reg;
}

lir_flag_t type_base_trans_alloc(type_kind kind) {
    if (is_float(kind)) {
        return LIR_FLAG_ALLOC_FLOAT;
    }

    return LIR_FLAG_ALLOC_INT;
}

reg_t *covert_alloc_reg(reg_t *reg) {
    if (reg->flag & FLAG(LIR_FLAG_ALLOC_FLOAT)) {
        return reg_find(reg->index, OWORD);
    }

    return reg_find(reg->index, QWORD);
}

