#include "test.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils/helper.h"

int setup() {
    printf("setup\n");
    return 0;
}

int teardown() {
    printf("teardown\n");
    return 0;
}

static void test_basic() {
//    char *output = BUILD_OUTPUT_NAME;
//    // linker new
//    elf_context *l = elf_context_new(output, OUTPUT_EXECUTABLE);
//
//    // 读取 main.o
//    int main_fd = open("./cases/linker/main.o", O_RDONLY | O_BINARY);
//    // 读取 crt1.o
//    int crt1_fd = open("/usr/local/musl/lib/crt1.o", O_RDONLY | O_BINARY);
//    // 读取 libc.a
//    int libc_fd = open("/usr/local/musl/lib/libc.a", O_RDONLY | O_BINARY);
//
//    elf_load_object_file(l, main_fd, 0);
//    elf_load_object_file(l, crt1_fd, 0);
//    elf_load_archive(l, libc_fd);
//
//    executable_file_format(l);
//
//    elf_output(l);
}

int main(void) {
    setup();
    test_basic();
    teardown();
}