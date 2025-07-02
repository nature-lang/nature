#ifndef NATURE_SRC_REGISTER_REGISTER_H_
#define NATURE_SRC_REGISTER_REGISTER_H_

#include "utils/helper.h"
#include "src/types.h"
#include "utils/slice.h"
#include "utils/table.h"
#include "src/build/config.h"
#include "utils/type.h"
#include "arch/amd64.h"
#include "arch/arm64.h"
#include "arch/riscv64.h"

// -------- reg start -----------
extern table_t *reg_table; // 根据 index 和 size 定位具体的寄存器
extern slice_t *regs;
extern reg_t *alloc_regs[UINT8_MAX];

static inline void reg_init() {
    reg_table = table_new();
    regs = slice_new();
    memset(alloc_regs, 0, sizeof(alloc_regs));

    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_reg_init();
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return arm64_reg_init();
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        return riscv64_reg_init();
    }

    assertf(false, "not support arch %d", BUILD_ARCH);
}

static inline uint8_t alloc_reg_count() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_ALLOC_REG_COUNT;
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return ARM64_ALLOC_REG_COUNT;
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        return RISCV64_ALLOC_REG_COUNT;
    }

    assertf(false, "not support arch %d", BUILD_ARCH);
    exit(1);
}

char *reg_table_key(lir_flag_t alloc_type, uint8_t index, uint8_t size);

reg_t *reg_select(uint8_t index, type_kind kind);

reg_t *reg_find(lir_flag_t alloc_type, uint8_t index, size_t size);

reg_t *covert_alloc_reg(reg_t *reg);

reg_t *reg_new(char *name, uint8_t index, lir_flag_t alloc_type, uint8_t size, uint8_t reg_id);

lir_flag_t type_kind_trans_alloc(type_kind kind);

#endif //NATURE_SRC_REGISTER_REGISTER_H_
