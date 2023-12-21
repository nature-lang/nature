#include <stdio.h>

#include "runtime/memory.h"
#include "runtime/processor.h.n"
#include "runtime/type/list.h"
#include "test.h"
#include "utils/custom_links.h"

addr_t rt_fn_main_base;

uint64_t rt_symdef_count;
symdef_t rt_symdef_data;

uint64_t rt_fndef_count;
fndef_t rt_fndef_data; // 仅需要修复一下 gc_bits 数据即可

uint64_t rt_rtype_count;
rtype_t rt_rtype_data;

char *build_entry = "main.n";

int setup() {
    // 调整工作目录到 cases 中
    char *work_dir = getenv("WORK_DIR");
    assertf(strlen(work_dir), "work_dir empty");
    VOID chdir(work_dir);
    build(build_entry);

    rt_symdef_count = ct_symdef_count;
    rt_symdef_ptr = (symdef_t *) ct_symdef_data;
    rt_fndef_count = ct_fndef_count;
    rt_fndef_ptr = (fndef_t *) ct_fndef_data;
    rt_rtype_count = ct_rtype_count;
    rt_rtype_ptr = (rtype_t *) ct_rtype_data;

    // runtime init
    processor_init();

    memory_init();

    return 0;
}

int teardown() {
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
static void _test_gc_basic() {
    DEBUG_STACK();
    // - 找到 cases 中的 list 对应的 rtype
    symbol_t *s = symbol_table_get("test_gc.main.local_list_1");
    ast_var_decl *var = s->ast_value;
    assert_int_equal(var->type.kind, TYPE_LIST);
    rtype_t rtype = ct_reflect_type(var->type);
    rtype_t element_rtype = ct_reflect_type(var->type.list->element_type);

    // - 调用 list_new 初始化需要的数据
    memory_list_t *l = list_new(rtype.index, element_rtype.index, 0);

    assert_int_equal((addr_t) l->array_data, (addr_t) 0xc000000000);
    assert_int_equal((addr_t) l, (addr_t) 0xc000002000);

    // l 中存储的是栈中的地址，而 &
    // 使用堆模拟 data 中的地址
    addr_t *data_size_addr = mallocz(sizeof(void *));
    *data_size_addr = (addr_t) l; // 将 l 的指存储到 data_size_addr 中

    // 随便找一个 symdef，将 指注册进去，从而至少能够触发 global symbol 维度的 gc
    symdef_t *symdef = &rt_symdef_ptr[0];
    symdef->need_gc = type_need_gc(var->type);
    symdef->size = type_sizeof(var->type); // 特殊标记
    symdef->base = (addr_t) data_size_addr;
    // 仅保留一个 symdef 用于测试，太多的话，实际上当前 debug 是软链接，并不存在
    rt_symdef_count = 1;

    int a = 23;
    list_push(l, &a);
    list_push(l, &a);
    list_push(l, &a);
    list_push(l, &a);
    list_push(l, &a);
    list_push(l, &a);
    list_push(l, &a);
    list_push(l, &a);
    list_push(l, &a);

    // 重新初始化 system_stack, 让 gc 完成后还能回来到此处

    // - 触发 gc
    runtime_gc();

    // 再次创建一个 list, 占用 rt_symdef_data 2
    // - 调用 list_new 初始化需要的数据
    memory_list_t *l2 = list_new(rtype.index, element_rtype.index, 0);
    assert_int_equal((addr_t) l2->array_data, (addr_t) 0xc000000000); // 由于 0xc000 被释放了，所以这里可以再次申请到该数据
    assert_int_equal((addr_t) l2, (addr_t) 0xc000002020);

    // 加入到 sym 中避免被清理
    data_size_addr = mallocz(sizeof(void *));
    *data_size_addr = (addr_t) l2; // 将 l 的指存储到 data_size_addr 中
    symdef = &rt_symdef_ptr[1];
    symdef->need_gc = type_need_gc(var->type);
    symdef->size = type_sizeof(var->type); // 特殊标记
    symdef->base = (addr_t) data_size_addr;
    // 仅保留一个 symdef 用于测试，太多的话，实际上当前 debug 是软链接，并不存在
    rt_symdef_count = 2;

    // 再次触发 gc
    runtime_gc();

    printf("hello\n");
}

// 切换到用户栈
static void test_gc_basic() {
    processor_t *p = processor_get();
    DEBUG_STACK();
    // 切换到 user mode
    MODE_CALL(p->user_mode, p->system_mode, _test_gc_basic)
}

int main(void) {
    setup();
    test_gc_basic();
    teardown();
}