#include "test.h"
#include <stdio.h>
#include "runtime/builtin/builtin.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/elf/elf.h"
#include "src/lower/amd64/amd64.h"
#include "src/lir/lir.h"


static void test_lower_hello_world() {
    amd64_register_init();
    opcode_init();
    amd64_lower_init();


    list *insts = list_new();
    closure *c = NEW(closure);

    // 编写 lir builtin_call print "hello world" => void
    // 参数构造（字符串的构造）
    lir_operand *first_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, "hello world!");
    // no result
    lir_op *call_op = lir_builtin_call(
            "print",
            NULL,
            1,
            first_param);

    // lir_lower
    list *asm_insts = amd64_lower(c, call_op);
    list_merge(insts, asm_insts);

    elf_init("hello.n");

    //  数据段编译(直接从 lower 中取还是从全局变量中取? 后者)
    elf_var_decl_list_build(amd64_decl_list);

    // 代码段
    elf_text_inst_list_build(insts);
    elf_text_inst_list_build(builtin_print());
    elf_text_inst_list_second_build();
    elf_t elf = elf_new();

    // 编码成二进制
    uint64_t count;
    uint8_t *binary = elf_encoding(elf, &count);
    // 输出到文件
    elf_to_file(binary, count, "builtin_print.o");
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_lower_hello_world),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}