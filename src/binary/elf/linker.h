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

#define addr_t uint64_t
/* special flag to indicate that the section should not be linked to the other ones */
#define SHF_PRIVATE 0x80000000
/* section is dynsymtab_section */
#define SHF_DYNSYM 0x40000000

#define ST_ASM_SET 0x04


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

    struct section_t *link; // 部分 section 需要 link 其他字段, 如符号表的 link 指向字符串表
    struct section_t *relocate; // 当前段指向的的重定位段,如当前段是 text,则 relocate 指向 .rela.text
    struct section_t *prev; // slice 中的上一个 section
} section_t;

typedef struct {
    section_t *section; // 引用全局 section
    uint64_t offset;
    bool is_new; // 是否为当前 object file 中第一次定义的 section
    bool link_once;
} object_section_t;

typedef struct {
    slice_t *sections;
    slice_t *private_sections;
    table *symbol_table; // 直接指向符号表 sym
    section_t *symtab_section;
    section_t *bss_section;
} linker_t;


/**
 * 加载归档文件
 * @param l
 */
void elf_load_archive(linker_t *l);

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
void elf_file_format();

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
size_t elf_section_data_forward(section_t *section, addr_t size, int align);

void *elf_section_realloc(section_t *section, uint64_t new_size);

uint64_t elf_set_sym(linker_t *l, Elf64_Sym *sym, char *name);

uint64_t elf_put_sym(linker_t *l,
                     Elf64_Sym *sym,
                     char *name);

uint64_t elf_put_str(section_t *s, char *str);

#endif //NATURE_LINKER_H
