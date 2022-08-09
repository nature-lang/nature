#ifndef NATURE_SRC_ASSEMBLER_ELF_ELF_H_
#define NATURE_SRC_ASSEMBLER_ELF_ELF_H_

#include "lib_elf.h"
#include "src/assembler/amd64/asm.h"
#include "src/assembler/amd64/opcode.h"
#include "utils/list.h"
#include "utils/table.h"

#define TEXT_INDEX 1
#define RELA_TEXT_INDEX 2
#define DATA_INDEX 3
#define SYMTAB_INDEX 4
#define SHSTRTAB_INDEX 6
#define SYMTAB_LAST_LOCAL_INDEX 3 // 符号表最后一个 local 字符的索引
#define SHDR_COUNT 7


typedef enum {
    ELF_SYMBOL_TYPE_VAR = 1,
    ELF_SYMBOL_TYPE_FN = 2,
//  ELF_SYMBOL_TYPE_SECTION,
//  ELF_SYMBOL_TYPE_FILE,
} elf_symbol_type;

typedef enum {
    ELF_SECTION_TEXT = TEXT_INDEX,
    ELF_SECTION_DATA = DATA_INDEX,
    ELF_SECTION_RELA_TEXT = RELA_TEXT_INDEX,
} elf_section;

/**
 * 指令存储结构
 * @param asm_inst
 */
typedef struct {
    uint8_t *data; // 指令二进制
    uint8_t count; // 指令长度
    uint64_t *offset; // 指令起始 offset
} linux_elf_text_inst_t;

/**
 * 全局符号表存储结构
 * @param asm_inst
 */
typedef struct {
    string name;
    uint64_t *offset;  // 符号所在偏移, 只有符号定义需要这个偏移地址,现阶段只有 text 段内偏移，改地址需要被修正
    uint8_t size;
    uint8_t *value; // 对于 var, 且其在数据段时，其可能会有预定义的值
    elf_symbol_type type;  // fn/var
    bool is_rel; // 是否引用外部符号
    bool is_local; // 是否是本地符号
    elf_section section; // 所在段，text/data
    int symtab_index; // 在符号表的索引,构建符号表时写入
} elf_symbol_t; // 用来构造符号表，以及数据段？

/**
 * 重定位表, 如果一个符号引用了外部符号多次，这里就需要记录多次
 * @param asm_inst
 */
typedef struct {
    string name;
    elf_symbol_type type; // 符号引用还是标签引用
    uint8_t section; // 使用符号
    uint64_t *offset;
    int8_t addend;
} elf_rel_t;

//list *elf_text_inst_list;
table *elf_symbol_table; // key: symbol_name, value list_node
list *elf_symbol_list; // list_node link
list *elf_rel_list;
string filename;
uint64_t global_text_offset; // 代码段偏移
uint64_t global_data_offset; // 数据段偏移

//typedef struct {
//    list *elf_text_inst_list;
//    table *elf_symbol_table; // key: symbol_name, value list_node
//    list *elf_symbol_list; // list_node link
//    list *elf_rel_list;
//    string filename;
//    uint64_t global_text_offset; // 代码段偏移
//    uint64_t global_data_offset; // 数据段偏移
//} elf_built_t;


void elf_init(string _filename);

void elf_var_decl_build(amd64_asm_var_decl decl);

void elf_var_decl_list_build(list *decl_list);

void elf_symbol_insert(elf_symbol_t *symbol);

uint64_t *elf_current_text_offset();

typedef struct {
    Elf64_Ehdr ehdr;
    uint8_t *data;
    uint8_t data_size;
    uint8_t *text;
    uint64_t text_size;
    char *shstrtab;
    Elf64_Shdr *shdr;
    uint8_t shdr_count;
    Elf64_Sym *symtab;
    uint64_t symtab_count;
    char *strtab;
    Elf64_Rela *rela_text;
    uint64_t real_text_count;
} elf_t;

/**
 * 文件头表
 * 代码段 (.text)
 * 数据段 (.data)
 * 段表字符串表 (.shstrtab)
 * 段表 (section header table)
 * 符号表 (.symtab)
 * 字符串表(.strtab)
 * 重定位表(.rel.text)
 * @return
 */
elf_t elf_new();

uint8_t *elf_encoding(elf_t elf, uint64_t *count);

void elf_to_file(uint8_t *binary, uint64_t count, string file);

/**
 * @param asm_data
 */
uint8_t *elf_data_build(uint64_t *size);

/**
 * 生成二进制结果
 * @param size
 * @return
 */
uint8_t *elf_text_build(uint64_t *size);

/**
 * 段表构建
 * @return
 */
string elf_shdr_build(uint64_t text_size,
                      uint64_t data_size,
                      uint64_t symtab_size,
                      uint64_t strtab_size,
                      uint64_t rela_text_size,
                      Elf64_Shdr *shdr);

/**
 * 重定位表构建
 * @param rel_text
 * @param count
 */
Elf64_Rela *elf_rela_text_build(uint64_t *count);

/**
 *
 * @param symtab 符号表和个数
 * @param count
 * @return 字符串表
 */
string elf_symtab_build(Elf64_Sym *symtab);

#endif //NATURE_SRC_ASSEMBLER_ELF_ELF_H_
