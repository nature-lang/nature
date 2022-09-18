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

void register_init() {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_register_init();
    }
}

reg_t *reg_new(char *name, uint8_t index, uint8_t size) {
    reg_t *reg = NEW(reg_t);
    reg->name = name;
    reg->index = index;
    reg->size = size;

    table_set(reg_table, reg_table_key(index, size), reg);
    slice_push(regs, reg);
    return reg;
}
