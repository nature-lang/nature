#include "linker.h"
#include "elf.h"
#include "utils/helper.h"
#include "utils/error.h"

#include <unistd.h>

void elf_load_object_file(linker_t *l, int fd, uint64_t file_offset) {
    lseek(fd, file_offset, SEEK_SET);
    Elf64_Ehdr ehdr; // 清 0
    Elf64_Shdr shdr;
    ssize_t size = full_read(fd, &ehdr, sizeof(ehdr));
    if (size != sizeof(ehdr)) {
        error_exit("[elf_load_object_file] size: %d not equal sizeof ehdr", size);
    }
    if (ehdr.e_type != ET_REL) {
        error_exit("[elf_load_object_file] ehdr.e_type not rel file");
    }

    // read section header table
    Elf64_Shdr *shdr_list = elf_file_load_data(fd, file_offset + ehdr.e_shoff, sizeof(*shdr) * ehdr.e_shnum);
    object_section_t *current_sections = malloc(sizeof(object_section_t) * ehdr.e_shnum);

    // 加载段表字符串表 e_shstrndx = Section header string table index
    shdr = shdr_list[ehdr.e_shstrndx];
    char *shstrtab = elf_file_load_data(fd, file_offset + shdr.sh_offset, shdr.sh_size);

    // 加载符号表(可能存在多个)
    for (int i = 0; i < ehdr.e_shnum; ++i) {
        shdr = shdr_list[i];
        if (shdr.sh_type != SHT_SYMTAB) {
            continue;
        }
    }

}
