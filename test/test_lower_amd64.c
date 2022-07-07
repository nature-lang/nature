#include "test.h"
#include <stdio.h>
#include "runtime/builtin/builtin.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/elf/elf.h"
#include "src/lower/amd64/amd64.h"
#include "src/lir/lir.h"


static void test_lower_hello_world() {
    register_init();
    opcode_init();
    list *insts = list_new();

    closure *c = NEW(closure);
    // builtin_call print "hello world" => void
    lir_operand *first = lir_new_label_operand("print");
    // 参数构造（字符串的构造）
    lir_operand_actual_param *params_operand = NEW(lir_operand_actual_param);
    params_operand->count = 1;
    params_operand->list[0] =  // 如何构造字符串对象(就像如何构造 map 对象一样)
    lir_operand * second = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);


//    lir_operand* second = lir_new_actual_param() //

//    lir_op_new(LIR_OP_TYPE_BUILTIN_CALL, )

    elf_init("hello.n");

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