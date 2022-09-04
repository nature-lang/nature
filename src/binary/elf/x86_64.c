#include "x86_64.h"
#include "elf.h"
#include "linker.h"
#include "utils/helper.h"

int x86_64_gotplt_entry_type(uint relocate_type) {
    switch (relocate_type) {
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
        case R_X86_64_COPY:
        case R_X86_64_RELATIVE:
            return NO_GOTPLT_ENTRY;

            /* The following relocs wouldn't normally need GOT or PLT
               slots, but we need them for simplicity in the link
               editor part.  See our caller for comments.  */
        case R_X86_64_32:
        case R_X86_64_32S:
        case R_X86_64_64:
        case R_X86_64_PC32:
        case R_X86_64_PC64:
            return AUTO_GOTPLT_ENTRY;

        case R_X86_64_GOTTPOFF:
            return BUILD_GOT_ONLY;

        case R_X86_64_GOT32:
        case R_X86_64_GOT64:
        case R_X86_64_GOTPC32:
        case R_X86_64_GOTPC64:
        case R_X86_64_GOTOFF64:
        case R_X86_64_GOTPCREL:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_TLSGD:
        case R_X86_64_TLSLD:
        case R_X86_64_DTPOFF32:
        case R_X86_64_TPOFF32:
        case R_X86_64_DTPOFF64:
        case R_X86_64_TPOFF64:
        case R_X86_64_REX_GOTPCRELX:
        case R_X86_64_PLT32:
        case R_X86_64_PLTOFF64:
            return ALWAYS_GOTPLT_ENTRY;
    }

    return -1;
}

uint x86_64_create_plt_entry(linker_t *l, uint got_offset, sym_attr_t *attr) {
    section_t *plt = l->plt;

    int modrm = 0x25;
    if (plt->data_count == 0) {
        uint8_t *p = elf_section_data_add_ptr(plt, 16);
        p[0] = 0xff; // pushl got + PTR_SIZE
        p[1] = modrm + 0x10;
        write32le(p + 2, 8);
        p[6] = 0xff;
        p[7] = modrm;
        write32le(p + 8, PTR_SIZE * 2);
    }
    uint plt_offset = plt->data_count;
    uint8_t plt_rel_offset = plt->relocate ? plt->relocate->data_count : 0;

    uint8_t *p = elf_section_data_add_ptr(plt, 16);
    p[0] = 0xff; /* jmp *(got + x) */
    p[1] = modrm;
    write32le(p + 2, got_offset);
    p[6] = 0x68; /* push $xxx */
    /* On x86-64, the relocation is referred to by _index_ */
    write32le(p + 7, plt_rel_offset / sizeof(Elf64_Rela) - 1);
    p[11] = 0xe9; /* jmp plt_start */
    write32le(p + 12, -(plt->data_count));
    return plt_offset;
}

int8_t x86_64_is_code_relocate(uint relocate_type) {
    switch (relocate_type) {
        case R_X86_64_32:
        case R_X86_64_32S:
        case R_X86_64_64:
        case R_X86_64_GOTPC32:
        case R_X86_64_GOTPC64:
        case R_X86_64_GOTPCREL:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_REX_GOTPCRELX:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_GOT32:
        case R_X86_64_GOT64:
        case R_X86_64_GLOB_DAT:
        case R_X86_64_COPY:
        case R_X86_64_RELATIVE:
        case R_X86_64_GOTOFF64:
        case R_X86_64_TLSGD:
        case R_X86_64_TLSLD:
        case R_X86_64_DTPOFF32:
        case R_X86_64_TPOFF32:
        case R_X86_64_DTPOFF64:
        case R_X86_64_TPOFF64:
            return 0;

        case R_X86_64_PC32:
        case R_X86_64_PC64:
        case R_X86_64_PLT32:
        case R_X86_64_PLTOFF64:
        case R_X86_64_JUMP_SLOT:
            return 1;
    }
    return -1;
}