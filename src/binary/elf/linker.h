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

#define SEC_TACK(_sh_index) ((section_t *) l->sections->take[_sh_index])

#define SEC_START(_type, _section) ((_type*) _section->data)
#define SEC_END(_type, _section) ((_type*) (_section->data + _section->data_count))

#define addr_t uint64_t

# define REL_SECTION_FMT ".rela%s"

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
    uint got_offset;
    uint plt_offset;
    uint64_t plt_sym;
    int dyn_index;
} sym_attr_t;

/**
 * 段表与相应的二进制数据合并
 */
typedef struct section_t {
    // Elf64_Shdr 原始字段继承
    uint sh_name; // 段名称，段表字符串表 offset ~ \0
    uint sh_type; // 段类型，
    uint sh_flags;
    uint sh_info;
    uint sh_addralign;
    uint sh_entsize;
    uint sh_size;
    uint sh_offset;
    addr_t sh_addr; // 可重定位地址


    uint64_t data_count; // 数据位置
    uint64_t data_capacity; // 极限容量
    uint8_t *data; // 段二进制数据
    int sh_index; // 段表索引
    char name[50]; // 段表名称字符串冗余

    // 排序字段
    int actual_sh_index;
    int actual_sh_weight;
    uint phdr_flags;

    struct section_t *link; // 部分 section 需要 link 其他字段, 如符号表的 link 指向字符串表
    struct section_t *relocate; // 当前段指向的的重定位段,如当前段是 text,则 relocate 指向 .rela.text
    struct section_t *prev; // slice 中的上一个 section
} section_t;

typedef struct {
    section_t *section; // 引用全局 section
    uint64_t offset;
    bool is_new; // 是否为当前 object file 中第一次定义的 section
    bool link_once;
} local_section_t;

typedef struct {
    slice_t *sections;
    slice_t *private_sections;
    table *symbol_table; // 直接指向符号表 sym
    section_t *symtab_section;
    sym_attr_t *sym_attrs;
    uint sym_attrs_count;
    section_t *bss_section;
    section_t *data_section;
    section_t *text_section;
//    section_t *rodata_section;
    section_t *got;
    section_t *plt;

    // 可执行文件构建字段
    Elf64_Phdr *phdr_list; // 程序头表
    uint phdr_count; // 程序头表数量

    uint64_t file_offset;
    char *output; // 完整路径名称
} linker_t;


/**
 * 加载归档文件
 * @param l
 */
void elf_load_archive(linker_t *l, int fd);

/**
 * 加载可重定位目标文件文件到全局 section 中
 */
void elf_load_object_file(linker_t *l, int fd, uint64_t file_offset);

/**
 * 从文件中加载数据到 section data 中
 * @param l
 */
void *elf_file_load_data(int fd, uint64_t offset, uint64_t size);

/**
 * 构造 elf 可执行文件结构,依旧是段结构数据
 */
void executable_file_format(linker_t *l);

section_t *elf_new_section(linker_t *l, char *name, uint sh_type, uint sh_flags);

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
size_t elf_section_data_forward(section_t *section, addr_t size, uint align);

void elf_section_realloc(section_t *section, uint64_t new_size);

uint64_t elf_set_sym(linker_t *l, Elf64_Sym *sym, char *name);

uint64_t elf_put_sym(linker_t *l,
                     Elf64_Sym *sym,
                     char *name);

uint64_t elf_put_str(section_t *s, char *str);

void elf_resolve_common_symbols(linker_t *l);

void elf_build_got_entries(linker_t *l, uint got_sym_index);

void elf_put_relocate(linker_t *l, section_t *sym_section, section_t *apply_section, uint64_t offset, int type,
                      int sym_index, int64_t addend);

void elf_relocate_symbols(linker_t *l, section_t *sym_section);

void elf_relocate_sections(linker_t *l);

void elf_relocate_section(linker_t *l, section_t *apply_section, section_t *rel_section);

sym_attr_t *elf_get_sym_attr(linker_t *l, uint sym_index, bool alloc);

addr_t elf_get_sym_addr(linker_t *l, char *name);

void elf_fill_got(linker_t *l);

void elf_fill_got_entry(linker_t *l, Elf64_Rela *rel);

int tidy_section_headers(linker_t *l);

void sort_symbols(linker_t *l, section_t *s);

linker_t *linker_new(char *output);

#endif //NATURE_LINKER_H
