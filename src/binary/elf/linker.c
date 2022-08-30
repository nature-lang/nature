#include "linker.h"
#include "elf.h"
#include "utils/helper.h"
#include "utils/error.h"

#include <unistd.h>

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
        local_sections[sh_index].s = l->symtab_section; // 通过 index 进行全局关联

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
            ptr = section->data + section->data_count;
            full_read(fd, ptr, sh_size);
        };
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

void *elf_section_data_read_ptr(section_t *section, uint64_t size) {
    size_t offset = elf_section_data_forward(section, size, 1);
    return section->data + offset;
}


