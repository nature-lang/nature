#include "test.h"
#include <stdio.h>
#include "utils/links.h"
#include "runtime/type/list.h"
#include "runtime/processor.h"

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

    rt_symdef_size = ct_symdef_size_;
    rt_symdef_data = ct_symdef_data_;
    rt_fndef_count = ct_fndef_count_;
    rt_fndef_data = ct_fndef_data_;
    rt_rtype_count = ct_rtype_count_;
    rt_rtype_data = ct_rtype_data_;

    // runtime init
    processor_init();

    memory_init();

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
static void test_gc_basic() {
    // - 找到 stubs 中的 list 对应的 rtype
    symbol_t *s = symbol_table_get("test_gc.main.local_list_1");
    ast_var_decl *var = s->ast_value;
    assert_int_equal(var->type.kind, TYPE_LIST);
    rtype_t rtype = ct_reflect_type(var->type);
    rtype_t element_rtype = ct_reflect_type(var->type.list_decl->element_type);

    // - 调用 list_new 初始化需要的数据
    memory_list_t *l = list_new(rtype.index, element_rtype.index, 0);

    assert_int_equal((addr_t) l->array_data, (addr_t) 0xc000000000);
    assert_int_equal((addr_t) l, (addr_t) 0xc000002000);

    // l 中存储的是栈中的地址，而 &
    // 使用堆模拟 data 中的地址
    addr_t *data_size_addr = mallocz(POINTER_SIZE);
    *data_size_addr = (addr_t) l; // 将 l 的指存储到 data_size_addr 中

    // 随便找一个 symdef，将 指注册进去，从而至少能够触发 global symbol 维度的 gc
    symdef_t symdef = rt_symdef_data[0];
    symdef.need_gc = type_need_gc(var->type);
    symdef.size = type_sizeof(var->type);
    symdef.base = (addr_t) data_size_addr;

    // TODO list push 从而触发 new 新的 array

    printf("hello\n");
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_gc_basic),
    };
    return cmocka_run_group_tests(tests, setup, teardown);
}