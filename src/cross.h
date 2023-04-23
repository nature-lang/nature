#ifndef NATURE_CROSS_H
#define NATURE_CROSS_H

#include "structs.h"
#include "utils/type.h"
#include "src/build/config.h"

// -------- reg init start -----------
table_t *reg_table; // 根据 index 和 size 定位具体的寄存器
slice_t *regs;
reg_t *alloc_regs[UINT8_MAX];

void amd64_reg_init();

static inline void cross_reg_init() {
    // 初始化
    reg_table = table_new();
    regs = slice_new();
    memset(alloc_regs, 0, sizeof(alloc_regs));

    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_reg_init();
        return;
    }

    assertf(false, "not support arch %d", BUILD_ARCH);
}
// -------- reg init end -----------


// -------- alloc init start -----------
#define AMD64_ALLOC_REG_COUNT 14+16;

static inline uint8_t cross_alloc_reg_count() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_ALLOC_REG_COUNT;
    }

    assertf(false, "not support arch %d", BUILD_ARCH);
    exit(1);
}
// -------- alloc init end -----------


// -------- reg select start -----------
reg_t *amd64_reg_select(uint8_t index, type_kind base);

/**
 * index 对应寄存器的 index， 不过同一个 index 会对应多个 register
 */
static inline reg_t *cross_reg_select(uint8_t index, type_kind kind) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_reg_select(index, kind);
    }

    assert(false && "not support arch");
}

// -------- reg select end -----------



// -------- native start -----------
void amd64_native(closure_t *c);

static inline void cross_native(closure_t *c) {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_native(c);
        return;
    }

    assert(false && "not support arch");
}
// -------- native end -----------

// -------- lower start -----------
void amd64_lower(closure_t *c);

static inline void cross_lower(closure_t *c) {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_lower(c);
        return;
    }

    assert(false && "not support arch");
}
// -------- lower end -----------


// -------- opcode init start -----------
void amd64_opcode_init();

/**
 * 1. 初始化 opcode_root
 * 2. 将所有的指令注册到 tree 中
 * @return
 */
static inline void cross_opcode_init() {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_opcode_init();
        return;
    }

    assert(false && "not support this arch");
}
// -------- opcode init end -----------

// -------- linker/elf start -----------

#define AMD64_ELF_START_ADDR 0x400000
#define AMD64_64_ELF_PAGE_SIZE 0x200000
#define AMD64_PTR_SIZE 8 // 单位 byte
#define AMD64_NUMBER_SIZE 8 // 单位 byte

uint64_t amd64_create_plt_entry(elf_context *ctx, uint64_t got_offset, sym_attr_t *attr);

int amd64_gotplt_entry_type(uint64_t relocate_type);

int8_t amd64_is_code_relocate(uint64_t relocate_type);

void amd64_relocate(elf_context *ctx, Elf64_Rela *rel, int type, uint8_t *ptr, addr_t addr, addr_t val);

/**
 * 经过两次遍历最终生成 section text、symbol、rela
 * @param ctx
 * @param operations amd64_opcode_t
 */
void amd64_operation_encodings(elf_context *ctx, slice_t *closures);

static inline uint64_t cross_create_plt_entry(elf_context *ctx, uint64_t got_offset, sym_attr_t *attr) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_create_plt_entry(ctx, got_offset, attr);
    }

    assert(false && "not support this arch");
}


static inline int8_t cross_is_code_relocate(uint64_t relocate_type) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_is_code_relocate(relocate_type);
    }
    assert(false && "not support this arch");
}

static inline int cross_got_rel_type(bool is_code_rel) {
    if (BUILD_ARCH == ARCH_AMD64) {
        if (is_code_rel) {
            return R_X86_64_JUMP_SLOT;
        } else {
            return R_X86_64_GLOB_DAT;
        }
    }
    assert(false && "not support this arch");
}

static inline int cross_gotplt_entry_type(uint64_t relocate_type) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_gotplt_entry_type(relocate_type);
    }
    assert(false && "not support this arch");
}

static inline uint8_t cross_ptr_size() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_PTR_SIZE;
    }

    // TODO 判断当前是否在 runtime 中
    return sizeof(void *);
//    assert(false && "not support this arch");
}

static inline uint8_t cross_number_size() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_NUMBER_SIZE;
    }
    return sizeof(int);
//    assert(false && "not support this arch");
}

static inline uint64_t cross_elf_start_addr() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_ELF_START_ADDR;
    }
    assert(false && "not support this arch");
}

static inline uint64_t cross_elf_page_size() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_64_ELF_PAGE_SIZE;
    }
    assert(false && "not support this arch");
}

static inline void cross_relocate(elf_context *l, Elf64_Rela *rel, int type, uint8_t *ptr, addr_t addr, addr_t val) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_relocate(l, rel, type, ptr, addr, val);
    }
    assert(false && "not support this arch");
}

static inline uint16_t cross_ehdr_machine() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return EM_X86_64;
    }
    assert(false && "not support this arch");
}

static inline void cross_opcode_encodings(elf_context *ctx, slice_t *closures) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_operation_encodings(ctx, closures);
    }
    assert(false && "not support this arch");
}
// -------- linker/elf end -----------


#endif //NATURE_CROSS_H
