#include "register.h"
#include "utils/helper.h"
#include "amd64.h"

static string reg_table_key(uint8_t index, uint8_t size) {
    uint16_t int_key = ((uint16_t) index << 8) | size;
    return itoa(int_key);
}

reg_t *register_find(uint8_t index, uint8_t size) {
    return table_get(reg_table, reg_table_key(index, size));
}

void reg_init() {
    // 初始化
    reg_table = table_new();
    regs = slice_new();
    memset(alloc_regs, 0, sizeof(alloc_regs));

    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_reg_init();
    }
}

uint8_t alloc_reg_count() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_ALLOC_REG_COUNT
    }
    return 0;
}


reg_t *reg_new(char *name, uint8_t index, uint8_t size, int8_t alloc_id) {
    reg_t *reg = NEW(reg_t);
    reg->name = name;
    reg->index = index;
    reg->size = size;
    reg->alloc_id = alloc_id; // 默认为 -1，表示不可分配

    if (alloc_id > 0) {
        alloc_regs[alloc_id] = reg;
    }

    table_set(reg_table, reg_table_key(index, size), reg);
    slice_push(regs, reg);
    return reg;
}

