#ifndef NATURE_LINKER_H
#define NATURE_LINKER_H

/**
 * 包含 loader 和 linker 两部分
 */
#include <stdlib.h>
#include <stdint.h>
#include "src/binary/elf/elf.h"
#include "utils/slice.h"
#include "utils/table.h"
#include "src/types.h"
#include "src/build/config.h"


#define CONFIG_NEW_MACHO 1

#define SEC_TACK(_sh_index) ((section_t *) ctx->sections->take[_sh_index])

#define SEC_START(_type, _section) ((_type*) _section->data)

#define SEC_END(_type, _section) ((_type*) (_section->data + _section->data_count))

#define addr_t uint64_t

# define REL_SECTION_FMT ".rela%s"

#define OUTPUT_EXE 1 // 可执行文件
#define OUTPUT_OBJ 2 // 目标文件(可重定位文件)


/* Whether to generate a GOT/PLT entry and when. NO_GOTPLT_ENTRY is first so
   that unknown relocation don't create a GOT or PLT entry */
enum gotplt_entry {
    NO_GOTPLT_ENTRY,    /* never generate (eg. GLOB_DAT & JMP_SLOT relocs) */
    BUILD_GOT_ONLY,    /* only build GOT (eg. TPOFF relocs) */
    AUTO_GOTPLT_ENTRY,    /* generate if sym is UNDEF */
    ALWAYS_GOTPLT_ENTRY    /* always generate (eg. PLTOFF relocs) */
};

/* relocation type for 32 bit data relocation */
#define R_DATA_32   R_X86_64_32S
#define R_DATA_PTR  R_X86_64_64
#define R_JMP_SLOT  R_X86_64_JUMP_SLOT
#define R_GLOB_DAT  R_X86_64_GLOB_DAT
#define R_COPY      R_X86_64_COPY
#define R_RELATIVE  R_X86_64_RELATIVE


/* special flag to indicate that the section should not be linked to the other ones */
#define SHF_PRIVATE 0x80000000
/* section is dynsymtab_section */
#define SHF_DYNSYM 0x40000000

#define ST_ASM_SET 0x04

#define ARMAG  "!<arch>\012"    /* For COFF and a.out archives */
#define ARFMAG "`\n"

typedef struct {
    char ar_name[16];           /* name of this member */
    char ar_date[12];           /* file mtime */
    char ar_uid[6];             /* owner uid; printed as decimal */
    char ar_gid[6];             /* owner gid; printed as decimal */
    char ar_mode[8];            /* file mode, printed as octal   */
    char ar_size[10];           /* file size, printed as decimal */
    char ar_fmag[2];            /* should contain ARFMAG */
} archive_header_t;


typedef struct {
    section_t *section; // 引用全局 section
    uint64_t offset;
    bool is_new; // 是否为当前 object file 中第一次定义的 section
    bool link_once;
} local_section_t;

/**
 * 加载归档文件
 * @param ctx
 */
void load_archive(linker_context *ctx, int fd);

/**
 * 加载可重定位目标文件文件到全局 section 中
 */
void load_object_file(linker_context *ctx, int fd, uint64_t file_offset);

/**
 * 从文件中加载数据到 section data 中
 * @param l
 */
void *elf_file_load_data(int fd, uint64_t offset, uint64_t size);


section_t *elf_new_section(linker_context *ctx, char *name, uint64_t sh_type, uint64_t sh_flags);

/**
 * 全局 section data 写入点
 * size 位需要写入的数据的长度
 * @return
 */
void *section_ptr_add(section_t *section, addr_t size);

/**
 * data_count forward
 * @return
 */
size_t elf_section_data_forward(section_t *section, addr_t size, uint64_t align);

void elf_section_realloc(section_t *section, uint64_t new_size);

uint64_t elf_set_sym(linker_context *ctx, Elf64_Sym *sym, char *name);

uint64_t elf_put_sym(section_t *symtab_section, table_t *symtab_hash, Elf64_Sym *sym, char *name);

uint64_t elf_put_str(section_t *s, char *str);

uint64_t elf_put_data(section_t *s, uint8_t *data, uint64_t size);

void elf_resolve_common_symbols(linker_context *ctx);

void elf_build_got_entries(linker_context *ctx, uint64_t got_sym_index);

Elf64_Rela *elf_put_relocate(linker_context *ctx, section_t *sym_section, section_t *apply_section,
                             uint64_t offset, int type, int sym_index, int64_t addend);

Elf64_Rela *
elf_put_rel_data(linker_context *ctx, section_t *apply_section, uint64_t rel_offset, char *name, uint64_t symbol_type);

void elf_relocate_symbols(linker_context *ctx, section_t *sym_section);

void elf_relocate_sections(linker_context *ctx);

void elf_relocate_section(linker_context *ctx, section_t *apply_section, section_t *rel_section);

sym_attr_t *elf_get_sym_attr(linker_context *ctx, uint64_t sym_index, bool alloc);

uint64_t elf_get_sym_addr(linker_context *ctx, char *name, bool force);

Elf64_Sym *elf_find_sym(linker_context *ctx, char *name);

void elf_fill_got(linker_context *ctx);

void elf_fill_got_entry(linker_context *ctx, Elf64_Rela *rel);

int tidy_section_headers(linker_context *ctx);

void sort_symbols(linker_context *ctx, section_t *s);

linker_context *linker_context_new(char *output, uint8_t type);

void alloc_section_names(linker_context *ctx, bool is_obj);

uint64_t elf_put_global_symbol(linker_context *ctx, char *name, void *value, uint8_t value_size);

uint64_t collect_fndef_list(linker_context *ctx);

uint64_t collect_symdef_list(linker_context *ctx);


#ifndef O_BINARY
# define O_BINARY 0
#endif

#define START_LABEL "_start"

/**
 * 包含可重定位文件和可执行文件的输出
 */
void elf_output(linker_context *ctx);

void macho_output(linker_context *ctx);

static inline void exe_file_output(linker_context *ctx) {
    if (BUILD_OS == OS_LINUX) {
        return elf_output(ctx);
    } else if (BUILD_OS == OS_DARWIN) {
        return macho_output(ctx);
    }

    assertf(false, "not support os '%s'", os_to_string(BUILD_OS));
}

/**
 * 构造 elf 可执行文件结构,依旧是段结构数据
 */
void elf_exe_file_format(linker_context *ctx);

void macho_exe_file_format(linker_context *ctx);

static inline void exe_file_format(linker_context *ctx) {
    if (BUILD_OS == OS_LINUX) {
        return elf_exe_file_format(ctx);
    } else if (BUILD_OS == OS_DARWIN) {
        return macho_exe_file_format(ctx);
    }

    assertf(false, "not support os '%s'", os_to_string(BUILD_OS));
}

static void *linker_realloc(void *ptr, unsigned long size) {
    void *ptr1;
    if (size == 0) {
        free(ptr);
        ptr1 = NULL;
    } else {
        ptr1 = realloc(ptr, size);
        assertf(ptr1, "memory full");
    }
    return ptr1;
}

#endif //NATURE_LINKER_H
