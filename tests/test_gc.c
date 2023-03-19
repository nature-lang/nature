#include "test.h"
#include <stdio.h>
#include "utils/links.h"

addr_t rt_fn_main_base;

uint64_t rt_symdef_size;
symdef_t *rt_symdef_data;

uint64_t rt_fndef_count;
fndef_t *rt_fndef_data; // 仅需要修复一下 gc_bits 数据即可

uint64_t rt_rtype_count;
rtype_t *rt_rtype_data;

char *build_entry = "main.n";

int setup(void **state) {
    // 调整工作目录到 stub 中
    char *work_dir = getenv("WORK_DIR");
    assertf(strlen(work_dir), "work_dir empty");
    chdir(work_dir);
    build(build_entry);

    rt_symdef_size = ct_symdef_size;
    rt_symdef_data = ct_symdef_data;
    rt_fndef_count = ct_fndef_count;
    rt_fndef_data = ct_fndef_data;
    rt_rtype_count = ct_rtype_count;
    rt_rtype_data = ct_rtype_data;

    return 0;
}

int teardown(void **state) {
    printf("teardown\n");
    return 0;
}

/**
 * "test_gc.main.inc_0"
 * "test_gc.main.local_list_1"
 * "test_gc.main.a_2"
 * "test_gc.foo.person"
 * "test_gc.foo.global_i"
 * "test_gc.foo.global_b"
 * "test_gc.foo.global_s"
 */
static void test_allocator() {
    // rt_type_data 中啥数据都有了，尝试分配一个 list 结构的数据(list_new)
    symbol_t *s = symbol_table_get("test_gc.main.local_list_1");
    ast_var_decl *var = s->ast_value;
    assert_int_equal(var->type.kind, TYPE_LIST);

    rtype_t rtype = ct_reflect_type(var->type);


    printf("hello\n");
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_allocator),
    };
    return cmocka_run_group_tests(tests, setup, teardown);
}