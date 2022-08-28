#ifndef NATURE_LINKER_H
#define NATURE_LINKER_H

/**
 * 包含 loader 和 linker 两部分
 */

#include <stdlib.h>
#include <stdint.h>
#include <elf.h>
#include "utils/slice.h"

#define addr_t uint64_t

/**
 * 段表与相应的二进制数据合并
 */
typedef struct section_t {
    // Elf64_Shdr 原始字段继承
    int sh_name; // 段名称，段表字符串表 offset ~ \0
    int sh_type; // 段类型，
    int sh_flags;
    int sh_info;
    int sh_addralign;
    int sh_entsize;
    int sh_size;
    int sh_offset;
    addr_t sh_addr; // 可重定位地址


    uint64_t data_count; // 数据位置
    uint64_t data_capacity; // 极限容量
    uint8_t *data; // 段二进制数据
    int index; // 段表索引
    char name[50]; // 段表名称字符串冗余

    struct section_t *link; // 部分 section 需要 link 其他字段, 如符号表的 link 指向字符串表
    struct section_t *relocate; // 当前短指向使用的重定位段, 比如 .rela.text
    struct section_t *prev; // slice 中的上一个 section
    struct section_t *hash; // 符号表 section 对应的 hash section
} section_t;

typedef struct {
    slice_t sections;
} linker_t;

/**
 * 加载可重定位目标文件文件
 */
void elf_load_object_file();


#endif //NATURE_LINKER_H
