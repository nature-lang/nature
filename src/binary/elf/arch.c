#include "arch.h"
#include "src/build/config.h"

int8_t is_code_relocate(uint relocate_type) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_is_code_relocate(relocate_type);
    }
    return -1;
}


int got_rel_type(bool is_code_rel) {
    if (BUILD_ARCH == ARCH_AMD64) {
        if (is_code_rel) {
            return R_X86_64_JUMP_SLOT;
        } else {
            return R_X86_64_GLOB_DAT;
        }
    }
    return -1;
}

int gotplt_entry_type(uint relocate_type) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_gotplt_entry_type(relocate_type);
    }
    return -1;
}

uint8_t ptr_size() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_PTR_SIZE;
    }
    return 0;
}

uint64_t elf_start_addr() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_ELF_START_ADDR;
    }
    return 0;
}

uint64_t elf_page_size() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_64_ELF_PAGE_SIZE;
    }
    return 0;
}

void relocate(elf_context *l, Elf64_Rela *rel, int type, uint8_t *ptr, addr_t addr, addr_t val) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_relocate(l, rel, type, ptr, addr, val);
    }
}

uint16_t ehdr_machine() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return EM_X86_64;
    }
    return 0;
}

void opcode_encodings(elf_context *ctx, slice_t *opcodes) {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_operation_encodings(ctx, opcodes);
        return;
    }
}
