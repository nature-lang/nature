#include "output.h"
#include "arch.h"
#include "utils/error.h"
#include "utils/helper.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

void output_executable_file(elf_context *l) {
    FILE *f;
    unlink(l->output);
    int fd = open(l->output, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0777);
    if (fd < 0 || (f = fdopen(fd, "wb")) == NULL) {
        error_exit("[output_executable_file] could not write '%s: %s'", l->output);
        return;
    }
    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    int shdr_count = l->sections->count;

    if (l->phdr_count > 0) {
        ehdr.e_phentsize = sizeof(Elf64_Phdr);
        ehdr.e_phnum = l->phdr_count;
        ehdr.e_phoff = sizeof(Elf64_Ehdr);
        shdr_count = tidy_section_headers(l);
    }

    l->file_offset = (l->file_offset + 3) & -4;

    // fill header
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;

    ehdr.e_type = ET_EXEC;
    ehdr.e_entry = elf_get_sym_addr(l, "_start");

    ehdr.e_machine = ehdr_machine();
    ehdr.e_version = EV_CURRENT;
    ehdr.e_shoff = l->file_offset;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = shdr_count;
    ehdr.e_shstrndx = shdr_count - 1;
    fwrite(&ehdr, 1, sizeof(Elf64_Ehdr), f);
    if (l->phdr_list) {
        fwrite(l->phdr_list, 1, l->phdr_count * sizeof(Elf64_Phdr), f);
    }
    uint64_t offset = sizeof(Elf64_Ehdr) + l->phdr_count * sizeof(Elf64_Phdr);
    sort_symbols(l, l->symtab_section);
    for (int sh_index = 1; sh_index < shdr_count; ++sh_index) {
        int order_index = SEC_TACK(sh_index)->actual_sh_index;
        section_t *s = SEC_TACK(order_index);
        if (s->sh_type != SHT_NOBITS) {
            while (offset < s->sh_offset) {
                fputc(0, f);
                offset++;
            }
            if (s->sh_size) {
                fwrite(s->data, 1, s->sh_size, f);
            }
            offset += s->sh_size;
        }
    }

    // output section headers
    while (offset < ehdr.e_shoff) {
        fputc(0, f);
        offset++;
    }
    for (int sh_index = 0; sh_index < shdr_count; ++sh_index) {
        Elf64_Shdr shdr;
        memset(&shdr, 0, sizeof(shdr));
        section_t *s = SEC_TACK(sh_index);
        if (s) {
            shdr.sh_name = s->sh_name;
            shdr.sh_type = s->sh_type;
            shdr.sh_flags = s->sh_flags;
            shdr.sh_entsize = s->sh_entsize;
            shdr.sh_info = s->sh_info;
            if (s->link) {
                shdr.sh_link = s->link->sh_index;
            }
            shdr.sh_addralign = s->sh_addralign;
            shdr.sh_addr = s->sh_addr;
            shdr.sh_offset = s->sh_offset;
            shdr.sh_size = s->sh_size;
        }
        fwrite(&shdr, 1, sizeof(Elf64_Shdr), f);
    }

    fclose(f);
}
