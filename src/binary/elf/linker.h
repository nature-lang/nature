#ifndef NATURE_LINKER_H
#define NATURE_LINKER_H

/**
 * 包含 loader 和 linker 两部分
 */
#include <stdlib.h>
#include <stdint.h>
#include <elf.h>
#include "utils/slice.h"
#include "utils/table.h"
#include "src/structs.h"

#define SEC_TACK(_sh_index) ((section_t *) ctx->sections->take[_sh_index])

#define SEC_START(_type, _section) ((_type*) _section->data)
#define SEC_END(_type, _section) ((_type*) (_section->data + _section->data_count))

#define addr_t uint64_t

# define REL_SECTION_FMT ".rela%s"

#define OUTPUT_EXECUTABLE 1 // 可执行文件
#define OUTPUT_OBJECT 2 // 目标文件(可重定位文件)


/* special flag to indicate that the section should not be linked to the other ones */
#define SHF_PRIVATE 0x80000000
/* section is dynsymtab_section */
#define SHF_DYNSYM 0x40000000

#define ST_ASM_SET 0x04

#define ARMAG  "!<arch>\012"    /* For COFF and a.out archives */
#define ARFMAG "`\n"

enum gotplt_entry {
    NO_GOTPLT_ENTRY,    /* never generate (eg. GLOB_DAT & JMP_SLOT relocs) */
    BUILD_GOT_ONLY,    /* only build GOT (eg. TPOFF relocs) */
    AUTO_GOTPLT_ENTRY,    /* generate if sym is UNDEF */
    ALWAYS_GOTPLT_ENTRY    /* always generate (eg. PLTOFF relocs) */
};

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
void elf_load_archive(elf_context *ctx, int fd);

/**
 * 加载可重定位目标文件文件到全局 section 中
 */
void elf_load_object_file(elf_context *ctx, int fd, uint64_t file_offset);

/**
 * 从文件中加载数据到 section data 中
 * @param l
 */
void *elf_file_load_data(int fd, uint64_t offset, uint64_t size);

/**
 * 构造 elf 可执行文件结构,依旧是段结构数据
 */
void executable_file_format(elf_context *ctx);

section_t *elf_new_section(elf_context *ctx, char *name, uint64_t sh_type, uint64_t sh_flags);

/**
 * 全局 section data 写入点
 * size 位需要写入的数据的长度
 * @return
 */
void *elf_section_data_add_ptr(section_t *section, addr_t size);

/**
 * data_count forward
 * @return
 */
size_t elf_section_data_forward(section_t *section, addr_t size, uint64_t align);

void elf_section_realloc(section_t *section, uint64_t new_size);

uint64_t elf_set_sym(elf_context *ctx, Elf64_Sym *sym, char *name);

uint64_t elf_put_sym(section_t *symtab_section, table_t *symtab_hash, Elf64_Sym *sym, char *name);

uint64_t elf_put_str(section_t *s, char *str);

uint64_t elf_put_data(section_t *s, uint8_t *data, uint64_t count);

void elf_resolve_common_symbols(elf_context *ctx);

void elf_build_got_entries(elf_context *ctx, uint64_t got_sym_index);

Elf64_Rela *elf_put_relocate(elf_context *ctx, section_t *sym_section, section_t *apply_section,
                             uint64_t offset, int type, int sym_index, int64_t addend);

void elf_relocate_symbols(elf_context *ctx, section_t *sym_section);

void elf_relocate_sections(elf_context *ctx);

void elf_relocate_section(elf_context *ctx, section_t *apply_section, section_t *rel_section);

sym_attr_t *elf_get_sym_attr(elf_context *ctx, uint64_t sym_index, bool alloc);

addr_t elf_get_sym_addr(elf_context *ctx, char *name);

Elf64_Sym *elf_find_sym(elf_context *ctx, char *name);

void elf_fill_got(elf_context *ctx);

void elf_fill_got_entry(elf_context *ctx, Elf64_Rela *rel);

int tidy_section_headers(elf_context *ctx);

void sort_symbols(elf_context *ctx, section_t *s);

elf_context *elf_context_new(char *output, uint8_t type);

void alloc_section_names(elf_context *ctx, bool is_obj);

void *elf_set_global_symbol(elf_context *ctx, char *name, void *value, uint8_t value_size);

#endif //NATURE_LINKER_H
