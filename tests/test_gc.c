#include "test.h"
#include <stdio.h>
#include "utils/links.h"

addr_t link_fn_main_base;

uint64_t link_symdef_size;
symdef_t *link_symdef_data;

uint64_t link_fndef_count;
fndef_t *link_fndef_data; // 仅需要修复一下 gc_bits 数据即可

uint64_t link_rtype_count;
reflect_type_t *link_rtype_data;

char *build_entry = "main.n";

int setup(void **state) {
    // 调整工作目录到 stub 中
    char *work_dir = getenv("WORK_DIR");
    assertf(strlen(work_dir), "work_dir empty");
    chdir(work_dir);
    build(build_entry);

    link_symdef_size = ct_symdef_size;
    link_symdef_data = ct_symdef_data;
    link_fndef_count = ct_fndef_count;
    link_fndef_data = ct_fndef_data;
    link_rtype_count = ct_rtype_count;
    link_rtype_data = ct_rtype_data;

    return 0;
}

int teardown(void **state) {
    printf("teardown\n");
    return 0;
}

static void test_allocator() {
    printf("hello\n");
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_allocator),
    };
    return cmocka_run_group_tests(tests, setup, teardown);
}