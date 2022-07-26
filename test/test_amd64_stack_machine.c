#include "test.h"
#include <stdio.h>
#include "runtime/type/string.h"
#include "runtime/type/type_debug.h"
#include "src/lib/list.h"
#include "src/lib/helper.h"
#include "src/lir/lir.h"
#include "src/assembler/amd64/asm.h"
#include "src/assembler/amd64/register.h"
#include "src/syntax/scanner.h"
#include "src/syntax/parser.h"
#include "src/semantic/analysis.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/lower/amd64/amd64.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/amd64/opcode.h"
#include "src/assembler/elf/elf.h"
#include "src/debug/debug.h"

static void built_target(string path, string name) {
    char *source = file_read(path);
    // scanner
    list *token_list = scanner(source);
    // parser
    ast_block_stmt stmt_list = parser(token_list);
    // analysis
    ast_closure_decl closure_decl = analysis(stmt_list);
    // infer 类型检查和推导
    infer(&closure_decl);

    // compiler to lir
    compiler_closures closures = compiler(&closure_decl);

    // construct cfg
    for (int i = 0; i < closures.count; ++i) {
        closure *c = closures.list[i];
        // 构造 cfg
        cfg(c);

        // 构造 ssa
//    ssa(c);
#ifdef DEBUG_CFG
        debug_cfg(closures.list[i]);
#endif
    }

    amd64_register_init();
    opcode_init();
    amd64_lower_init();

    list *insts = list_new();
    for (int i = 0; i < closures.count; ++i) {
        closure *c = closures.list[i];
        list_append(insts, amd64_lower_closure(c));
    }

    elf_init(name);
    //  数据段编译(直接从 lower 中取还是从全局变量中取? 后者)
    elf_var_decl_list_build(amd64_decl_list);
    // 代码段
    elf_text_inst_list_build(insts);
    elf_text_inst_list_second_build();
    elf_t elf = elf_new();
    // 编码成二进制
    uint64_t count;
    uint8_t *binary = elf_encoding(elf, &count);
    // 输出到文件
    elf_to_file(binary, count, str_connect(name, ".o"));
}

static void test_hello() {
    built_target("/home/vagrant/Code/nature/test/stubs/0_hello.n", "hello.n");
}

static void test_sum() {
    built_target("/home/vagrant/Code/nature/test/stubs/1_sum.n", "sum.n");
}

static void test_call() {
    built_target("/home/vagrant/Code/nature/test/stubs/2_call.n", "call.n");
}

static void test_if() {
    built_target("/home/vagrant/Code/nature/test/stubs/3_if.n", "if.n");
}

static void test_while() {
    built_target("/home/vagrant/Code/nature/test/stubs/4_while.n", "while.n");
}

static void test_closure() {
    built_target("/home/vagrant/Code/nature/test/stubs/5_closure.n", "closure.n");
}

int main(void) {
    const struct CMUnitTest tests[] = {
//            cmocka_unit_test(test_hello),
//            cmocka_unit_test(test_sum),
//            cmocka_unit_test(test_call),
//            cmocka_unit_test(test_if),
//            cmocka_unit_test(test_while),
            cmocka_unit_test(test_closure),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}