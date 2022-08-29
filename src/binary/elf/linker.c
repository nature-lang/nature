#include "linker.h"

#include <unistd.h>

void elf_load_object_file(linker_t *l, int fd, uint64_t file_offset) {
    lseek(fd, file_offset, SEEK_SET);

}
