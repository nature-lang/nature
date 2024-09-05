#ifndef NATURE_CROSS_H
#define NATURE_CROSS_H

#include "types.h"
#include "src/build/config.h"
#include "src/lir.h"

// -------- reg start -----------
extern table_t *reg_table; // 根据 index 和 size 定位具体的寄存器
extern slice_t *regs;
extern reg_t *alloc_regs[UINT8_MAX];

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


// -------- cross_alloc_reg_count start -----------
#define AMD64_ALLOC_REG_COUNT 14+16;

static inline uint8_t cross_alloc_reg_count() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_ALLOC_REG_COUNT;
    }

    assertf(false, "not support arch %d", BUILD_ARCH);
    exit(1);
}
// -------- cross_alloc_reg_count end -----------


// -------- alloc kind start -----------
alloc_kind_e amd64_alloc_kind_of_def(closure_t *c, lir_op_t *op, lir_var_t *var);

alloc_kind_e amd64_alloc_kind_of_use(closure_t *c, lir_op_t *op, lir_var_t *var);

static inline alloc_kind_e cross_alloc_kind_of_def(closure_t *c, lir_op_t *op, lir_var_t *var) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_alloc_kind_of_def(c, op, var);
    }

    assert(false && "not support arch");
}

static inline alloc_kind_e cross_alloc_kind_of_use(closure_t *c, lir_op_t *op, lir_var_t *var) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_alloc_kind_of_use(c, op, var);
    }

    assert(false && "not support arch");
}


// -------- alloc kind end -----------


// -------- reg select start -----------
reg_t *amd64_reg_select(uint8_t index, type_kind kind);

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


//static inline


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

    assert(false && "not support arch");
}
// -------- opcode init end -----------

// -------- linker/elf start -----------

#define AMD64_ELF_START_ADDR 0x400000
#define AMD64_64_ELF_PAGE_SIZE 0x200000
#define AMD64_PTR_SIZE 8 // 单位 byte
#define AMD64_NUMBER_SIZE 8 // 单位 byte

uint64_t amd64_create_plt_entry(elf_context_t *ctx, uint64_t got_offset, sym_attr_t *attr);

int amd64_gotplt_entry_type(uint64_t relocate_type);

int arm64_gotplt_entry_type(uint64_t relocate_type);

int8_t amd64_is_code_relocate(uint64_t relocate_type);

int8_t arm64_is_code_relocate(uint64_t relocate_type);

void amd64_relocate(elf_context_t *ctx, Elf64_Rela *rel, int type, uint8_t *ptr, addr_t addr, addr_t val);

static inline uint64_t cross_create_plt_entry(elf_context_t *ctx, uint64_t got_offset, sym_attr_t *attr) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_create_plt_entry(ctx, got_offset, attr);
    }

    assert(false && "not support arch");
}


static inline int8_t cross_is_code_relocate(uint64_t relocate_type) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_is_code_relocate(relocate_type);
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return arm64_is_code_relocate(relocate_type);
    }
    assert(false && "not support arch");
}

static inline int cross_got_rel_type(bool is_code_rel) {
    if (BUILD_ARCH == ARCH_AMD64) {
        if (is_code_rel) {
            return R_X86_64_JUMP_SLOT;
        } else {
            return R_X86_64_GLOB_DAT;
        }
    }
    assert(false && "not support arch");
}

static inline int cross_gotplt_entry_type(uint64_t relocate_type) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_gotplt_entry_type(relocate_type);
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return arm64_gotplt_entry_type(relocate_type);
    }
    assert(false && "not support arch");
}

static inline uint8_t cross_ptr_size() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_PTR_SIZE;
    }

    return sizeof(void *);
//    assert(false && "not support arch");
}

#ifndef POINTER_SIZE
#define POINTER_SIZE cross_ptr_size()
#endif

static inline uint8_t cross_number_size() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_NUMBER_SIZE;
    }
    return sizeof(int);
//    assert(false && "not support arch");
}

static inline uint64_t cross_elf_start_addr() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_ELF_START_ADDR;
    }
    assert(false && "not support arch");
}

static inline uint64_t cross_elf_page_size() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_64_ELF_PAGE_SIZE;
    }
    assert(false && "not support arch");
}

static inline void cross_relocate(elf_context_t *l, Elf64_Rela *rel, int type, uint8_t *ptr, addr_t addr, addr_t val) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_relocate(l, rel, type, ptr, addr, val);
    }
    assert(false && "not support arch");
}

static inline uint16_t cross_ehdr_machine() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return EM_X86_64;
    }
    assert(false && "not support arch");
}

// -------- linker/elf end -----------

static inline type_kind cross_kind_trans(type_kind kind) {
    if (BUILD_ARCH == ARCH_AMD64) {
        if (kind == TYPE_FLOAT) {
            return TYPE_FLOAT64;
        }
        if (kind == TYPE_INT) {
            return TYPE_INT64;
        }
        if (kind == TYPE_UINT) {
            return TYPE_UINT64;
        }

        return kind;
    }
    assert(false && "not support arch");
}

#endif //NATURE_CROSS_H
