#include "test.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "src/binary/elf/linker.h"
#include "src/binary/elf/output.h"
#include "utils/helper.h"

int setup(void **state) {
    printf("setup\n");
    return 0;
}

int teardown(void **state) {
    printf("teardown\n");
    return 0;
}

static void test_basic() {
    char *output = "main";
    // linker new
    elf_context *l = linker_new(output);

    // 读取 main.o
    int main_fd = open("./stubs/linker/main.o", O_RDONLY | O_BINARY);
    // 读取 crt1.o
    int crt1_fd = open("/usr/local/musl/lib/crt1.o", O_RDONLY | O_BINARY);
    // 读取 libc.a
    int libc_fd = open("/usr/local/musl/lib/libc.a", O_RDONLY | O_BINARY);

    elf_load_object_file(l, main_fd, 0);
    elf_load_object_file(l, crt1_fd, 0);
    elf_load_archive(l, libc_fd);

    executable_file_format(l);

    output_executable_file(l);
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_basic),
    };
    return cmocka_run_group_tests(tests, setup, teardown);
}