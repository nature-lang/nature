#include "test.h"
#include "src/build/build.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/ssa.h"
#include "utils/helper.h"
#include "src/debug/debug.h"
#include "src/register/linearscan.h"
#include "src/native/native.h"
#include "src/lower/lower.h"

#include <stdio.h>
#include <unistd.h>

#ifndef DEBUG_CFG
#define DEBUG_CFG true
#endif

int setup(void **state) {
    printf("setup\n");
    setenv("NATURE_ROOT", "/home/vagrant/Code/nature/debug", 1);
    return 0;
}

int teardown(void **state) {
    printf("teardown\n");
    return 0;
}

static void test_basic() {
    char buf[256];
    getcwd(buf, 256);

    char *work_dir = buf;
    chdir(work_dir);

    char *source_path = file_join(work_dir, "20221025_11_reg_alloc.n");

    env_init();
    config_init();

    printf("NATURE_ROOT: %s\n", NATURE_ROOT);
    printf("BUILD_OS: %s\n", os_to_string(BUILD_OS));
    printf("BUILD_ARCH: %s\n", arch_to_string(BUILD_ARCH));
    printf("BUILD_OUTPUT: %s\n", BUILD_OUTPUT);
    printf("WORK_DIR: %s\n", WORK_DIR);
    printf("BASE_NS: %s\n", BASE_NS);
    printf("TERM_DIR: %s\n", TEMP_DIR);
    printf("source_path: %s\n", source_path);

    // 初始化全局符号表
    symbol_init();
    // lir init
    lir_init();
    // 初始化寄存器列表
    reg_init();

    module_t *m = module_build(source_path, true);
    m->closures = slice_new();
    // 全局符号的定义也需要推导以下原始类型
    for (int j = 0; j < m->symbols->count; ++j) {
        symbol_t *s = m->symbols->take[j];
        if (s->type != SYMBOL_TYPE_VAR) {
            continue;
        }
        infer_var_decl(s->decl); // 类型还原
    }
    for (int j = 0; j < m->ast_closures->count; ++j) {
        ast_closure_t *closure = m->ast_closures->take[j];
        // 类型推断
        infer(closure);
        // 编译
        slice_append_free(m->closures, compiler(closure)); // 都写入到 compiler_closure 中了
    }

    for (int j = 0; j < m->closures->count; ++j) {
        closure_t *c = m->closures->take[j];
        c->module = m;

        // 构造 cfg
        cfg(c);

        // 构造 ssa
        ssa(c);

        lower(c);

        // 寄存器分配
        linear_scan(c);

        debug_lir(c);

        // native
        native(c);

        debug_asm(c);
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_basic),
    };
    return cmocka_run_group_tests(tests, setup, teardown);
}