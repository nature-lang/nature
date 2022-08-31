#include "linker.h"
#include "elf.h"
#include "utils/helper.h"
#include "utils/error.h"

#include <unistd.h>
#include <string.h>

void elf_load_object_file(linker_t *l, int fd, uint64_t file_offset) {
    lseek(fd, file_offset, SEEK_SET);
    Elf64_Ehdr ehdr; // 清 0
    Elf64_Shdr shdr;
    ssize_t read_size = full_read(fd, &ehdr, sizeof(ehdr));
    if (read_size != sizeof(ehdr)) {
        error_exit("[elf_load_object_file] size: %d not equal sizeof ehdr", read_size);
    }
    if (ehdr.e_type != ET_REL) {
        error_exit("[elf_load_object_file] ehdr.e_type not rel file");
    }

    // read section header table
    Elf64_Shdr *shdr_list = elf_file_load_data(fd, file_offset + ehdr.e_shoff, sizeof(shdr) * ehdr.e_shnum);
    object_section_t *local_sections = malloc(sizeof(object_section_t) * ehdr.e_shnum);

    // 加载段表字符串表 e_shstrndx = Section header string table index
    shdr = shdr_list[ehdr.e_shstrndx];
    char *shstrtab = elf_file_load_data(fd, file_offset + shdr.sh_offset, shdr.sh_size);

    // 加载符号表,单个可中定位文件中只允许存在一个符号表
    Elf64_Sym *symtab = NULL;
    Elf64_Sym *sym;
    uint64_t sym_count = 0;
    char *strtab = NULL;
    for (int sh_index = 0; sh_index < ehdr.e_shnum; ++sh_index) {
        shdr = shdr_list[sh_index];
        if (shdr.sh_type != SHT_SYMTAB) {
            continue;
        }
        if (symtab) {
            error_exit("[elf_load_object_file] object must contain only on symtab");
        }

        sym_count = shdr.sh_size / sizeof(*sym);
        // 加载符号数据
        symtab = elf_file_load_data(fd, file_offset + shdr.sh_offset, shdr.sh_size);
        local_sections[sh_index].section = l->symtab_section; // 通过 index 进行全局关联

        // 加载符号字符串表
        shdr = shdr_list[shdr.sh_link];
        strtab = elf_file_load_data(fd, file_offset + shdr.sh_offset, shdr.sh_size);
    }

    // 段合并(按段名称合并，而不是段的类型, 其中不包含符号表，主要是对 data、text 段的合并)
    for (int sh_index; sh_index < ehdr.e_shnum; ++sh_index) {
        // 跳过段表字符串表
        if (sh_index == ehdr.e_shstrndx) {
            continue;
        }

        shdr = shdr_list[sh_index];
        if (shdr.sh_type == SHT_RELA) {
            shdr = shdr_list[shdr.sh_info];
        }

        if (shdr.sh_type != SHT_PROGBITS &&
            shdr.sh_type != SHT_NOTE &&
            shdr.sh_type != SHT_NOBITS &&
            shdr.sh_type != SHT_PREINIT_ARRAY &&
            shdr.sh_type != SHT_INIT_ARRAY &&
            shdr.sh_type != SHT_FINI_ARRAY &&
            !str_equal(shstrtab + shdr.sh_name, ".stabstr")) {
            continue;
        }

        shdr = shdr_list[sh_index];
        char *shdr_name = shstrtab + shdr.sh_name;
        section_t *section;
        // n * n 的遍历查找
        for (int i = 1; i < l->sections->count; ++i) {
            section = l->sections->take[i];
            if (str_equal(section->name, shdr_name)) {
                // 在全局 sections 中找到了同名 section
                goto FOUND;
            }
        }
        // not found
        section = elf_new_section(l, shdr_name, shdr.sh_type, shdr.sh_flags & ~SHF_GROUP);
        section->sh_addralign = shdr.sh_addralign;
        section->sh_entsize = shdr.sh_entsize;
        local_sections[sh_index].is_new = true;
        FOUND:
        // 同名但是类型不相同
        if (shdr.sh_type != section->sh_type) {
            error_exit("[elf_load_object_file] sh %s type invalid", shdr_name);
        }
        // align
        section->data_count += -section->data_count & (shdr.sh_addralign - 1);
        // local shdr > global section
        if (shdr.sh_addralign > section->sh_addralign) {
            section->sh_addralign = shdr.sh_addralign;
        }
        local_sections[sh_index].offset = section->data_count;
        local_sections[sh_index].section = section;

        // 将 local section 数据写入到全局 section 中
        uint64_t sh_size = shdr.sh_size;
        if (shdr.sh_type == SHT_NOBITS) { // 预留空间单没数据
            section->data_count += sh_size;
        } else {
            unsigned char *ptr;
            lseek(fd, file_offset + shdr.sh_offset, SEEK_SET); // 移动 fd 指向的文件的偏移点
            ptr = elf_section_data_add_ptr(section, sh_size);
            full_read(fd, ptr, sh_size);
        };
    }

    // 完善 section 中的关联关系，比如 section.link
    for (int sh_index; sh_index < ehdr.e_shnum; ++sh_index) {
        section_t *section = local_sections[sh_index].section;
        if (!section || !local_sections[sh_index].is_new) {
            continue;
        }

        shdr = shdr_list[sh_index];
        if (shdr.sh_link > 0) {
            section->link = local_sections[shdr.sh_link].section; // 这里的 section 为全局 section
        }

        // 重定位段表的 sh_link 保存对应的符号表
        // sh_info 保存重定位表的目标表，如果 .rel.text -> .text
        if (shdr.sh_type == SHT_RELA) {
            // 修正为 global section 中的目标
            section_t *target_section = local_sections[shdr.sh_info].section;
            section->sh_info = target_section->sh_index;
            // 添加反向 link, section 已经是经过合并操作的全局唯一 section 了
            target_section->relocate = section;
        }
    }

    // 符号重定位
    uint64_t *symtab_index_map = malloc(sizeof(uint64_t) * sym_count);
    // 遍历所有符号(符号表的第一个符号为 NULL)
    sym = symtab + 1;
    for (int i = 0; i < sym_count; ++i, sym++) {
        if (sym->st_shndx != SHN_UNDEF && sym->st_shndx < SHN_LORESERVE) {
            object_section_t local = local_sections[sym->st_shndx]; // st_shndx 定义符号的段
            if (!local.section) {
                continue;
            }

            // convert section index
            sym->st_shndx = local.section->sh_index;
            // sm->offset 是 section' data 合并到全局 section 时的起始地址，所以 st_value 自然要加上这个起始地址
            sym->st_value += local.offset;
        }
        // add symbol
        char *sym_name = strtab + sym->st_name;
        // 当前目标文件中定义的符号
        uint64_t sym_index = elf_set_sym(l, sym, sym_name);
        symtab_index_map[i] = sym_index;
    }

    // rela patch
    // 当然并不是所有的符号都已经被 patch 了，不过没关系，先指向 undef 就行，一旦符号被加载进来就会自动修复的，而不是重新建
    for (int sh_index = 0; sh_index < ehdr.e_shnum; ++sh_index) {
        section_t *s = local_sections[sh_index].section;
        if (!s) {
            continue;
        }
        if (s->sh_type != SHT_RELA) {
            continue;
        }

        shdr = shdr_list[sh_index];
        uint64_t offset = local_sections[sh_index].offset;
        uint64_t sh_size = shdr.sh_size;

        // 应用 rel 的 section
        uint64_t apply_offset = local_sections[shdr.sh_info].offset;
        Elf64_Rel *rel = (Elf64_Rel *) s->data + offset;
        Elf64_Rel *rel_end = (Elf64_Rel *) s->data + offset + sh_size;
        while (rel < rel_end) {
            uint64_t rel_type = ELF64_R_TYPE(rel->r_info); // 重定位类型
            uint64_t sym_index = ELF64_R_SYM(rel->r_info); // 重定位符号在符号表中的索引

            if (sym_index >= sym_count) {
                error_exit("[elf_load_object_file] rel sym index exception");
            }

            sym_index = symtab_index_map[sym_index];
            if (!sym_index) {
                error_exit("[elf_load_object_file] sym index not found");
            }

            rel->r_info = ELF64_R_INFO(sym_index, rel_type);
            rel->r_offset = apply_offset;
            rel++;
        }
    }
}

size_t elf_section_data_forward(section_t *section, uint64_t size, int align) {
    size_t offset, offset_end;
    offset = (section->data_count + align - 1) & -align;
    offset_end = offset + size;
    if (section->sh_type != SHT_NOBITS && offset_end > section->data_capacity) {
        elf_section_realloc(section, offset_end);
    }
    section->data_count = offset_end;
    if (align > section->sh_addralign) {
        section->sh_addralign = align;
    }

    return offset;
}

void *elf_section_data_add_ptr(section_t *section, uint64_t size) {
    size_t offset = elf_section_data_forward(section, size, 1);
    return section->data + offset;
}

uint64_t elf_set_sym(linker_t *l, Elf64_Sym *sym, char *name) {

    section_t *s = l->symtab_section;
    table *symbol_table = l->symbol_table;
    uint sym_bind = ELF64_ST_BIND(sym->st_info);
    uint sym_type = ELF64_ST_TYPE(sym->st_info);
    uint8_t sym_visible = ELF64_ST_VISIBILITY(sym->st_other);
    Elf64_Sym exist_sym;
    uint64_t sym_index;

    if (sym_bind != STB_LOCAL) {
        // 全局搜索符号查看是否已经被定义
        sym_index = (uint64_t) table_get(symbol_table, name);
        if (sym_index == 0) {
            goto DEF;
        }
        exist_sym = ((Elf64_Sym *) s->data)[sym_index];

        if (exist_sym.st_value == sym->st_value &&
            exist_sym.st_size == sym->st_size &&
            exist_sym.st_info == sym->st_info &&
            exist_sym.st_other == sym->st_other &&
            exist_sym.st_shndx == sym->st_shndx) {
            return sym_index;
        }

        if (exist_sym.st_shndx == SHN_UNDEF) {
            exist_sym.st_other = sym->st_other;
            goto PATCH;
        } else {
            // 当前段中的符号与全局符号同名

            // 计算可见性
            uint exist_sym_bind = ELF64_ST_BIND(exist_sym.st_info);
            uint8_t exist_sym_visible = ELF64_ST_VISIBILITY(exist_sym.st_other);
            /* propagate the most constraining visibility */ // 使用约束力最强的可见性等级,越往右约束性越强
            /* STV_DEFAULT(0)<STV_PROTECTED(3)<STV_HIDDEN(2)<STV_INTERNAL(1) */
            uint8_t new_visible;
            if (sym_visible == STV_DEFAULT) {
                new_visible = exist_sym_visible;
            } else {
                // 排除 default 的情况下，值越小约束性越强
                new_visible = (exist_sym_visible < sym_visible) ? exist_sym_visible : sym_visible;
            }
            exist_sym.st_other = (exist_sym.st_other & ~ELF64_ST_VISIBILITY(-1)) | new_visible;

            // 同名覆盖关系
            if (sym->st_shndx == SHN_UNDEF) {
                // 保留原有符号，什么都不做
                return sym_index;
            } else if (sym_bind == STB_GLOBAL && exist_sym_bind == STB_WEAK) {
                // 强符号覆盖弱符号
                goto PATCH;
            } else if (sym_bind == STB_WEAK && exist_sym_bind == STB_GLOBAL) {
                return sym_index;
            } else if (sym_bind == STB_WEAK && exist_sym_bind == STB_WEAK) {
                return sym_index;
            } else if (sym_visible == STV_HIDDEN || sym_visible == STV_INTERNAL) {
                return sym_index;
            } else if ((exist_sym.st_shndx == SHN_COMMON || exist_sym.st_shndx == l->bss_section->sh_index)
                       && (sym->st_shndx < SHN_LORESERVE
                           && sym->st_shndx != l->bss_section->sh_index)) {
                goto PATCH;
            } else if (sym->st_shndx == SHN_COMMON || sym->st_shndx == l->bss_section->sh_index) {
                return sym_index;
            } else if (s->sh_flags & SHF_DYNSYM) {
                return sym_index;
            } else if (exist_sym.st_other & ST_ASM_SET) {
                goto PATCH;
            } else {
                error_exit("symbol %s repeat defined", name);
            }
        }
    }

    PATCH:
    exist_sym.st_info = ELF64_ST_INFO(sym_bind, sym_type);
    exist_sym.st_shndx = sym->st_shndx;
    exist_sym.st_value = sym->st_value;
    exist_sym.st_size = sym->st_size;
    return sym_index;

    DEF:
    sym_index = elf_put_sym(l, sym, name);
    return sym_index;
}

uint64_t elf_put_sym(linker_t *l, Elf64_Sym *sym, char *name) {
    section_t *s = l->symtab_section;
    Elf64_Sym *new_sym = elf_section_data_add_ptr(s, sizeof(Elf64_Sym));
    uint64_t name_offset = 0;
    if (name && name[0]) {
        name_offset = elf_put_str(s->link, name);
    }

    new_sym->st_name = name_offset;
    new_sym->st_value = sym->st_value;
    new_sym->st_size = sym->st_size;
    new_sym->st_info = sym->st_info;
    new_sym->st_other = sym->st_other;
    new_sym->st_shndx = sym->st_shndx;
    uint64_t sym_index = new_sym - (Elf64_Sym *) s->data; // 终点 - 起点 = 数量

    table_set(l->symbol_table, name, (void *) sym_index);

    return sym_index;
}

uint64_t elf_put_str(section_t *s, char *str) {
    size_t len = strlen(str) + 1; // 预留一个 \0
    uint64_t offset = s->data_count;

    char *ptr = elf_section_data_add_ptr(s, len);
    memmove(ptr, str, len);
    return offset;
}


