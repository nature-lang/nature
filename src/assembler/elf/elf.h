#ifndef NATURE_SRC_ASSEMBLER_ELF_ELF_H_
#define NATURE_SRC_ASSEMBLER_ELF_ELF_H_

#include "src/assembler/amd64/asm.h"
#include "lib_elf.h"
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

#define ELF_TEXT_INST_NEW(_asm_inst) ({ \
   elf_text_inst_t *_inst = NEW(elf_text_inst_t); \
  _inst->data = NULL; \
  _inst->count = 0; \
  _inst->offset = 0;                    \
  _inst->asm_inst = asm_inst; \
  _inst->rel_operand = NULL; \
  _inst->rel_symbol = NULL; \
  _inst->may_need_reduce = false; \
  _inst->reduce_count = 0; \
  _inst;\
})

string filename;

uint64_t global_text_offset; // 代码段偏移
uint64_t global_data_offset; // 数据段偏移

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
    asm_inst_t asm_inst; // 原始指令, 指令改写与二次扫描时使用
    string rel_symbol; // 使用的符号
    asm_operand_t *rel_operand; // 引用自 asm_inst
    bool may_need_reduce; // jmp 指令可以从 rel32 优化为 rel8
    uint8_t reduce_count; // jmp rel32 => jmp rel8 导致的指令的长度的变化差值
} elf_text_inst_t;

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

list *elf_text_inst_list;
table *elf_symbol_table; // key: symbol_name, value list_node
list *elf_symbol_list; // list_node link
list *elf_rel_list;

void elf_init(string _filename);

void elf_var_decl_build(asm_var_decl decl);

void elf_var_decl_list_build(list *decl_list);

void elf_text_label_build(asm_inst_t asm_inst, uint64_t *offset);

// 如果 asm_inst 的参数是 label 或者 inst.as = label 需要进行符号注册与处理
// 其中需要一个 link 结构来引用最近 128 个字节的指令，做 jmp rel 跳转，原则上不能影响原来的指令
// 符号表的收集工作，符号表收集需要记录偏移地址，所以如果存在修改，也需要涉及到这里的数据修改
void elf_text_inst_build(asm_inst_t asm_inst, uint64_t *offset);

void elf_text_inst_list_build(list *asm_inst_list); // 一次构建基于 asm_inst 列表
void elf_text_inst_list_second_build(); // 二次构建(基于 elf_text_inst_list)

void elf_symbol_insert(elf_symbol_t *symbol);

void elf_confirm_text_rel(string name);

/**
 * rel32 to rel8, count - 3
 * @param t
 */
void elf_rewrite_text_rel(elf_text_inst_t *t);

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

void elf_to_file(uint8_t *binary, uint64_t count, string filename);

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
