#include "elf.h"
#include "utils/helper.h"
#include "utils/error.h"
#include <string.h>
#include <stdio.h>
#include "amd64_inst.h"

void linux_elf_symbol_insert(elf_symbol_t *symbol) {
    table_set(linux_elf_symbol_table, symbol->name, symbol);
    list_push(linux_elf_symbol_list, symbol);
}

uint64_t *linux_elf_current_data_offset() {
    uint64_t *offset = NEW(uint64_t);
    *offset = global_data_offset;
    return offset;
}

static char *linux_elf_header_ident() {
    char *ident = malloc(sizeof(char) * EI_NIDENT);
    memset(ident, 0, EI_NIDENT);

    ident[0] = 0x7F; // del 符号的编码
    ident[1] = 'E';
    ident[2] = 'L';
    ident[3] = 'F';
    ident[4] = ELFCLASS64; // elf 文件类型: 64 位
    ident[5] = ELFDATA2LSB; // 字节序： 小端
    ident[6] = EV_CURRENT; // elf 版本号
    ident[7] = ELFOSABI_NONE; // os abi = unix v
    ident[8] = 0; // ABI version
    return ident;
}

linux_elf_t linux_elf_new() {
    // 数据段构建(依旧是遍历符号表)
    uint64_t data_size = 0;
    uint8_t *data = linux_elf_data_build(&data_size);

    // 代码段构建 .text
    uint64_t text_size = 0;
    uint8_t *text = linux_elf_text_build(&text_size);

    // 符号表构建(首先计算符号的数量)
    uint64_t symtab_count = 4;
    list_node *current = linux_elf_symbol_list->front;
    while (current->value != NULL) {
        elf_symbol_t *s = current->value;
        if (!s->is_local) {
            symtab_count++;
        }
        current = current->next;
    }
    Elf64_Sym *symtab = malloc(sizeof(Elf64_Sym) * symtab_count);
    string strtab = linux_elf_symtab_build(symtab);

    // 代码段重定位表构建
    uint64_t rel_text_count;
    Elf64_Rela *rel_text = linux_elf_rela_text_build(&rel_text_count);

    // 段表构建
    Elf64_Shdr *shdr = malloc(sizeof(Elf64_Shdr) * SHDR_COUNT);
    string shstrtab = linux_elf_shdr_build(text_size,
                                           data_size,
                                           symtab_count * sizeof(Elf64_Sym),
                                           strlen(strtab),
                                           rel_text_count * sizeof(Elf64_Rela),
                                           shdr);

    Elf64_Off shoff = sizeof(Elf64_Ehdr) + text_size + data_size + strlen(shstrtab);
    // 文件头构建
    Elf64_Ehdr ehdr = {
            .e_ident = {
                    0x7F, // del 符号编码
                    'E',
                    'L',
                    'F',
                    ELFCLASS64,  // elf 文件类型: 64 位
                    ELFDATA2LSB, // 字节序： 小端
                    EV_CURRENT,   // elf 版本号
                    ELFOSABI_NONE, // os abi = unix v
                    0, // ABI version
            },
            .e_type = ET_REL, // elf 文件类型 = 可重定位文件
            .e_machine = EM_X86_64,
            .e_version = EV_CURRENT,
            .e_entry = 0, // elf 文件程序入口的线性绝对地址，一般用于可执行文件，可重定位文件配置为 0 即可
            .e_phoff = 0, // 程序头表在文件中的偏移，对于可重定位文件来说，值同样为 0，
            .e_shoff = shoff, // 段表在文件中偏移地址
            .e_flags = 0, // elf 平台相关熟悉，设置为 0 即可
            .e_ehsize = sizeof(Elf64_Ehdr), // 文件头表的大小
            .e_phentsize = 0, // 程序头表项的大小, 可重定位表没有这个头
            .e_phnum = 0, // 程序头表项, 这个只能是 0
            .e_shentsize = sizeof(Elf64_Shdr), // 段表项的大小
            .e_shnum = SHDR_COUNT, // 段表项数
            .e_shstrndx = SHSTRTAB_INDEX, // 段表字符串表的索引
    };

    // 输出二进制
    return (linux_elf_t) {
            .ehdr = ehdr,
            .text = text,
            .text_size = text_size,
            .data = data,
            .data_size = data_size,
            .shstrtab = shstrtab,
            .shdr = shdr,
            .shdr_count = SHDR_COUNT,
            .symtab = symtab,
            .symtab_count = symtab_count,
            .strtab = strtab,
            .rela_text = rel_text,
            .real_text_count = rel_text_count
    };
}

/**
 * 包含的项：
 * .text/.data/.rel.text/.shstrtab/.symtab/.strtab
 * @param text_size
 * @param symtab_size
 * @param strtab_size
 * @param rel_text_size
 * @return
 */
char *linux_elf_shdr_build(uint64_t text_size,
                           uint64_t data_size,
                           uint64_t symtab_size,
                           uint64_t strtab_size,
                           uint64_t rela_text_size,
                           Elf64_Shdr *shdr) {

    // 段表字符串表
    char *shstrtab_data = " ";
    uint64_t rela_text_name = strlen(shstrtab_data);
    uint64_t text_name = 6;
    shstrtab_data = str_connect(shstrtab_data, ".rela.text ");
    uint64_t data_name = strlen(shstrtab_data);
    shstrtab_data = str_connect(shstrtab_data, ".data ");
    uint64_t shstrtab_name = strlen(shstrtab_data);
    shstrtab_data = str_connect(shstrtab_data, ".shstrtab ");
    uint64_t symtab_name = strlen(shstrtab_data);
    shstrtab_data = str_connect(shstrtab_data, ".symtab ");
    uint64_t strtab_name = strlen(shstrtab_data);
    shstrtab_data = str_connect(shstrtab_data, ".strtab ");

    uint64_t offset = sizeof(Elf64_Ehdr);
    // 符号表偏移
    uint64_t text_offset = offset;
    offset += text_size;
    // 数据段在文件中的偏移
    uint64_t data_offset = offset;
    offset += data_size;
    // 段表字符串表偏移
    uint64_t shstrtab_offset = offset;
    offset += strlen(shstrtab_data);
    // 段表偏移
    uint64_t shdr_offset = offset;
    offset += SHDR_COUNT * sizeof(Elf64_Shdr);
    // 符号表偏移
    uint64_t symtab_offset = offset;
    offset += symtab_size;
    // 字符串表偏移
    uint64_t strtab_offset = offset;
    offset += strtab_size;
    // 重定位表偏移
    uint64_t rela_text_offset = offset;
    offset += rela_text_size;



//  Elf64_Shdr **section_table = malloc(sizeof(Elf64_Shdr) * 5);

    // 空段
    shdr[0] = (Elf64_Shdr) {
            .sh_name = 0,
            .sh_type = 0, // 表示程序段
            .sh_flags = 0,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = 0,
            .sh_size = 0,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 0,
            .sh_entsize = 0
    };

    // 代码段
    shdr[1] = (Elf64_Shdr) {
            .sh_name = text_name,
            .sh_type = SHT_PROGBITS, // 表示程序段
            .sh_flags = SHF_ALLOC | SHF_EXECINSTR,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = text_offset,
            .sh_size = text_size,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0
    };

    // 代码段重定位表
    shdr[2] = (Elf64_Shdr) {
            .sh_name = rela_text_name,
            .sh_type = SHT_RELA, // 表示程序段
            .sh_flags = SHF_INFO_LINK,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = rela_text_offset,
            .sh_size = rela_text_size,
            .sh_link = 4,
            .sh_info = 1,
            .sh_addralign = 8,
            .sh_entsize = sizeof(Elf64_Rela)
    };

    // 数据段
    shdr[3] = (Elf64_Shdr) {
            .sh_name = data_name,
            .sh_type = SHT_PROGBITS, // 表示程序段
            .sh_flags =  SHF_ALLOC | SHF_WRITE,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = data_offset,
            .sh_size = data_size,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 4,
            .sh_entsize = 0
    };

    // 符号表段
    shdr[4] = (Elf64_Shdr) {
            .sh_name = symtab_name,
            .sh_type = SHT_SYMTAB, // 表示程序段
            .sh_flags =  0,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = symtab_offset,
            .sh_size = symtab_size,
            .sh_link = 5,
            .sh_info = SYMTAB_LAST_LOCAL_INDEX + 1, // 符号表最后一个 local 符号的索引
            .sh_addralign = 8,
            .sh_entsize = sizeof(Elf64_Sym)
    };

    // 字符串串表 5
    shdr[5] = (Elf64_Shdr) {
            .sh_name = strtab_name,
            .sh_type = SHT_STRTAB, // 表示程序段
            .sh_flags =  0,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = strtab_offset,
            .sh_size = strtab_size,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0,
    };


    // 段表字符串表 6
    shdr[6] = (Elf64_Shdr) {
            .sh_name = shstrtab_name,
            .sh_type = SHT_STRTAB, // 表示程序段
            .sh_flags =  0,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = shstrtab_offset,
            .sh_size = strlen(shstrtab_data),
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0,
    };

    return shstrtab_data;
}

char *linux_elf_symtab_build(Elf64_Sym *symtab) {
    // 内部初始化
//  symbol = malloc(sizeof(symbol) * size);
    int index = 0;

    // 字符串表
    char *strtab_data = " ";

    // 0: NULL
    symtab[index++] = (Elf64_Sym) {
            .st_name = 0, // 字符串表的偏移
            .st_value = 0, // 符号相对于所在段基址的偏移
            .st_size = 0, // 符号的大小，单位字节
            .st_info = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE),
            .st_other = 0,
            .st_shndx = 0, // 符号所在段，在段表内的索引
    };

    // 1: file
    symtab[index++] = (Elf64_Sym) {
            .st_name = strlen(strtab_data),
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_LOCAL, STT_FILE),
            .st_other = 0,
            .st_shndx = SHN_ABS,
    };
    strtab_data = str_connect(strtab_data, filename);
    strtab_data = str_connect(strtab_data, " ");

    // 2: section: 1 = .text
    symtab[index++] = (Elf64_Sym) {
            .st_name = 0,
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
            .st_other = 0,
            .st_shndx = TEXT_INDEX,
    };

    // 3: section: 3 = .data
    symtab[index++] = (Elf64_Sym) {
            .st_name = 0,
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
            .st_other = 0,
            .st_shndx = DATA_INDEX,
    };

    // 4. 填充其余符号(list 遍历)
    list_node *current = linux_elf_symbol_list->front;
    while (current->value != NULL) {
        elf_symbol_t *s = current->value;
        if (!s->is_local) {
            Elf64_Sym sym = {
                    .st_name = strlen(strtab_data),
                    .st_size = s->size,
                    .st_info = ELF64_ST_INFO(STB_GLOBAL, s->type),
                    .st_other = 0,
                    .st_shndx = s->section,
            };
            if (s->offset != NULL) {
                sym.st_value = *s->offset;
            }
            int temp = index++;
            symtab[temp] = sym;
            s->symtab_index = temp;
            strtab_data = str_connect(strtab_data, s->name);
            strtab_data = str_connect(strtab_data, " ");
        }
        current = current->next;
    }

    return strtab_data;
}

Elf64_Rela *linux_elf_rela_text_build(uint64_t *count) {
    Elf64_Rela *r = malloc(sizeof(Elf64_Rela) * linux_elf_rel_list->count);
    *count = linux_elf_rel_list->count;
    list_node *current = linux_elf_rel_list->front;
    int i = 0;
    while (current->value != NULL) {
        elf_rel_t *rel = current->value;
        // 宿友的 elf 都必须在符号表中找到,因为 rela_text 中的 info 存储着在符号表中的索引
        elf_symbol_t *s = table_get(linux_elf_symbol_table, rel->name);
        if (s == NULL) {
            error_exit(
                    "[linux_elf_rela_text_build] not found symbol %s in table, all rel symbol must store to symbol table",
                    rel->name);
        }
        uint64_t index = s->symtab_index;
        // r_sym 表示重定位项在符号表内的索引(?)
        r[i] = (Elf64_Rela) {
                .r_offset = *rel->offset,
                .r_info = ELF64_R_INFO(index, R_X86_64_PC32),
                .r_addend = rel->addend,
        };
        if (rel->type == ELF_SYMBOL_TYPE_VAR) {
            // TODO 此处有些不可理喻了
            r[i].r_offset += 3; // mov 0x0(%rip),%rsi = 48 8b 35 00 00 00 00，其中偏移是从第四个字符开始
        }

        i++;
        current = current->next;
    }

    return r;
}

uint8_t *linux_elf_encoding(linux_elf_t elf, uint64_t *count) {
    *count = sizeof(Elf64_Ehdr) +
             elf.data_size +
             elf.text_size +
             strlen(elf.shstrtab) +
             sizeof(Elf64_Shdr) * elf.shdr_count +
             sizeof(Elf64_Sym) * elf.symtab_count +
             strlen(elf.strtab) +
             sizeof(Elf64_Rela) * elf.real_text_count;
    uint8_t *binary = malloc(*count);

    // 文件头
    uint8_t *p = binary;
    memcpy(p, &elf.ehdr, sizeof(Elf64_Ehdr));
    p += sizeof(Elf64_Ehdr);

    // 代码段
    memcpy(p, elf.text, elf.text_size);
    p += elf.text_size;

    // 数据段
    memcpy(p, elf.data, elf.data_size);
    p += elf.data_size;

    // 段表字符串表
    size_t len = strlen(elf.shstrtab);
    str_replace(elf.shstrtab, 32, 0);
    memcpy(p, elf.shstrtab, len);
    p += len;

    // 段表
    memcpy(p, elf.shdr, sizeof(Elf64_Shdr) * elf.shdr_count);
    p += sizeof(Elf64_Shdr) * elf.shdr_count;

    // 符号表
    memcpy(p, elf.symtab, sizeof(Elf64_Sym) * elf.symtab_count);
    p += sizeof(Elf64_Sym) * elf.symtab_count;

    // 字符串表
    len = strlen(elf.strtab);
    str_replace(elf.strtab, 32, 0);
    memcpy(p, elf.strtab, len);
    p += len;

    // 重定位表
    memcpy(p, elf.rela_text, sizeof(Elf64_Rela) * elf.real_text_count);
    p += sizeof(Elf64_Rela) * elf.real_text_count;

    return binary;
}

void linux_elf_to_file(uint8_t *binary, uint64_t count, char *file) {
    FILE *f = fopen(file, "w+b");
    fwrite(binary, 1, count, f);
    fclose(f);
}

uint8_t *linux_elf_text_build(uint64_t *size) {
    *size = 0;
    list_node *current = linux_elf_text_inst_list->front;
    while (current->value != NULL) {
        linux_elf_text_inst_t *inst = current->value;
        *size += inst->count;
        current = current->next;
    }

    uint8_t *text = malloc(sizeof(uint8_t) * *size);
    if (*size == 0) {
        return text;
    }

    uint8_t *p = text;

    current = linux_elf_text_inst_list->front;
    while (current->value != NULL) {
        linux_elf_text_inst_t *inst = current->value;
        memcpy(p, inst->data, inst->count);
        p += inst->count;
        current = current->next;
    }

    return text;
}

/**
 * 写入 custom 符号表即可
 * @param decl
 */
void linux_elf_var_decl_build(lower_var_decl_t decl) {
    elf_symbol_t *symbol = NEW(elf_symbol_t);
    symbol->name = decl.name;
    symbol->type = ELF_SYMBOL_TYPE_VAR;
    symbol->section = ELF_SECTION_DATA;
    symbol->offset = linux_elf_current_data_offset();
    global_data_offset += decl.size;
    symbol->size = decl.size;
    symbol->value = decl.value;
    symbol->is_rel = false;
    symbol->is_local = false; // data 段的都是全局符号，可以被其他文件引用
    linux_elf_symbol_insert(symbol);
//    linux_elf_amd64_confirm_text_rel(symbol->as); // TODO 符号表需要指定重排吗？
}

void linux_elf_var_decl_list_build(list *decl_list) {
    if (list_empty(decl_list)) {
        return;
    }
    list_node *current = decl_list->front;
    while (current->value != NULL) {
        lower_var_decl_t *decl = current->value;
        linux_elf_var_decl_build(*decl);
        current = current->next;
    }
}

uint8_t *linux_elf_data_build(uint64_t *size) {
    // 遍历符号表计算数量并申请内存
    list_node *current = linux_elf_symbol_list->front;
    while (current->value != NULL) {
        elf_symbol_t *t = current->value;
        if (t->type != ELF_SYMBOL_TYPE_VAR) {
            current = current->next;
            continue;
        }
        *size += t->size;
        current = current->next;
    }
    // 按 4 字节对齐
    if (*size > 0) {
        *size = (*size - *size % 4) + 4;
    }

    uint8_t *data = malloc(*size);
    uint8_t *p = data;

    current = linux_elf_symbol_list->front;
    while (current->value != NULL) {
        elf_symbol_t *symbol = current->value;
        if (symbol->type != ELF_SYMBOL_TYPE_VAR) {
            current = current->next;
            continue;
        }

        if (symbol->value != NULL) {
            memcpy(p, symbol->value, symbol->size);
        }
        p += symbol->size;
        current = current->next;
    }

    return data;
}

void linux_elf_init(char *_filename, list *var_decl_list) {
    filename = _filename;
    // 按 cpu 架构选择编译
    linux_elf_symbol_table = table_new();
    linux_elf_symbol_list = list_new();
    linux_elf_rel_list = list_new();

    global_data_offset = 0;
    global_text_offset = 0;

    // 符号表构造 -> linux_elf_symbol_list
    linux_elf_var_decl_list_build(var_decl_list);
}
