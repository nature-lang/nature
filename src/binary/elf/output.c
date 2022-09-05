#include "output.h"
#include "utils/error.h"
#include "utils/helper.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

void output_executable_file(linker_t *l, char *filename, uint64_t file_offset) {
    FILE *f;
    unlink(filename);
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0777);
    if (fd < 0 || (f = fdopen(fd, "wb")) == NULL) {
        error_exit("[output_executable_file] could not write '%s: %s'", filename);
        return;
    }
    Elf64_Ehdr ehdr;
    Elf64_Shdr shdr;
    memset(&ehdr, 0, sizeof(ehdr));

    if (l->phdr_count > 0) {
        ehdr.e_phentsize = sizeof(Elf64_Phdr);
        ehdr.e_phnum = l->phdr_count;
        ehdr.e_phoff = sizeof(Elf64_Ehdr);
//        shdr_count = tidy_section_headers(l);
    }

    fclose(f);
}
