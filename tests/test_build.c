#include "test.h"
#include "src/binary/elf/linker.h"
#include "src/build/build.h"
#include "utils/helper.h"

#include <stdio.h>
#include <unistd.h>

int setup() {
    printf("setup\n");
    setenv("NATURE_ROOT", "/home/vagrant/Code/nature/debug", 1);
    return 0;
}

int teardown() {
    printf("teardown\n");
    return 0;
}

static void test_basic() {
    char buf[256];
    VOID getcwd(buf, 256);
    char *work_dir = path_join(buf, "cases/build");
    VOID chdir(work_dir);

    // TODO 根据环境变量动态选择

    char *build_entry = "main.n";
    build(build_entry);
}

int main(void) {
    setup();
    test_basic();
    teardown();
}