#include "test.h"
#include <stdio.h>
#include "runtime/builtin/builtin.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/elf/elf.h"
#include "src/lower/amd64/amd64.h"
#include "src/lir/lir.h"

static lir_operand *test_lir_temp(char *ident, int stack_frame_offset, int8_t reg_id) {
    lir_operand *operand = NEW(lir_operand);
    operand->type = LIR_OPERAND_TYPE_VAR;
    lir_operand_var *var = NEW(lir_operand_var);
    var->old = ident;
    var->ident = ident;
    if (stack_frame_offset >= 0) {
        var->stack_frame_offset = stack_frame_offset;
    }
    if (reg_id >= 0) {
        var->reg_id = reg_id;
    }

    operand->value = var;
    return operand;
}

static list *test_elf_main_insts(uint8_t stack_offset) {
    list *insts = list_new();
//    list_push(insts, ASM_INST("label", { SYMBOL("_start", true, false) }));
    list_push(insts, ASM_INST("label", { SYMBOL("main", true, false) }));
    list_push(insts, ASM_INST("push", { REG(rbp) }));
    list_push(insts, ASM_INST("mov", { REG(rbp), REG(rsp) })); // 保存栈指针
    list_push(insts, ASM_INST("sub", { REG(rsp), UINT32(stack_offset) })); // 防止其他函数调用占用这一段栈空间
}

static list *test_elf_return_insts() {
    list *insts = list_new();
    list_push(insts, ASM_INST("mov", { REG(rsp), REG(rbp) }));
    list_push(insts, ASM_INST("pop", { REG(rbp) }));
    list_push(insts, ASM_INST("ret", {}));

    return insts;
}

static list *test_elf_exit_insts() {
    list *insts = list_new();
//    list_push(insts, ASM_INST("mov", { REG(rsp), REG(rbp) }));
//    list_push(insts, ASM_INST("pop", { REG(rbp) }));
    list_push(insts, ASM_INST("call", { LABEL("exit") }));
//    list_push(insts, ASM_INST("mov", { REG(eax), UINT32(60) }));
//    list_push(insts, ASM_INST("mov", { REG(rdi), UINT32(0) }));
//    list_push(insts, ASM_INST("syscall", {}));
    return insts;
}

static void test_lower_hello() {
    amd64_register_init();
    opcode_init();
    amd64_lower_init();


//    list *insts = list_new();
    list *insts = test_elf_main_insts(16);
    closure *c = NEW(closure);

    // test
//    list_push(insts, ASM_INST("mov", { REG(rax), UINT32(0) }));
//    list_push(insts, ASM_INST("call", { LABEL("builtin_print") }));
    /**
     * lir:
     *  runtime_call string_new(imm:"hello world") => tmp_0(分配栈偏移)
     *  builtin_call builtin_print(tmp_0)
     */
    char *str = "nature is best\n";
    lir_operand *temp_var = test_lir_temp("temp_1", 8, -1); // var 的 size 是多少？
    lir_op *string_new_op = lir_op_call(RUNTIME_CALL_STRING_NEW, temp_var, 2,
                                        LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, str),
                                        LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, strlen(str)));

    lir_op *print_op = lir_op_call("builtin_print", NULL, 1, temp_var);

    // lir_lower
    list_append(insts, amd64_lower(c, string_new_op));
    list_append(insts, amd64_lower(c, print_op));

    // exit
    list_append(insts, test_elf_return_insts());

    elf_init("hello_world.n");
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
    elf_to_file(binary, count, "hello_world.o");
}

static void test_lower_debug_printf() {
    amd64_register_init();
    opcode_init();
    amd64_lower_init();


//    list *insts = list_new();
    list *insts = test_elf_main_insts(16);
    closure *c = NEW(closure);

    // test
//    list_push(insts, ASM_INST("mov", { REG(rax), UINT32(0) }));
//    list_push(insts, ASM_INST("call", { LABEL("builtin_print") }));
    /**
     * lir:
     *  runtime_call string_new(imm:"hello world") => tmp_0(分配栈偏移)
     *  builtin_call builtin_print(tmp_0)
     */
    char *str = "nature is best: %d\n";
    lir_op *debug_printf_op = lir_op_call("debug_printf",
                                          NULL,
                                          2,
                                          LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, str),
                                          LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 2333));

    // lir_lower
    list_append(insts, amd64_lower(c, debug_printf_op));

    // exit
    list_append(insts, test_elf_return_insts());

    elf_init("debug_printf.n");
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
    elf_to_file(binary, count, "debug_printf.o");
}

static void test_lower_sum() {
    amd64_register_init();
    opcode_init();
    amd64_lower_init();


//    list *insts = list_new();
    list *insts = test_elf_main_insts(16);
    closure *c = NEW(closure);

    /**
     * lir:
     * var
     *  runtime_call string_new(imm:"hello world") => tmp_0(分配栈偏移)
     *  builtin_call builtin_print(tmp_0)
     */
    lir_operand *temp_var = test_lir_temp("sum_1", 8, -1); // var 的 size 是多少？
    lir_op *sum_op = lir_op_new(LIR_OP_TYPE_ADD,
                                LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 22),
                                LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 33),
                                temp_var);
    sum_op->data_type = TYPE_INT;
    sum_op->size = QWORD;

    lir_op *debug = lir_op_call("debug_printf", NULL, 2,
                                LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, "add 22 + 33 = %d\n"),
                                temp_var);

    // lir_lower
    list_append(insts, amd64_lower(c, sum_op));
    list_append(insts, amd64_lower(c, debug));

    // exit
    list_append(insts, test_elf_return_insts());

    elf_init("sum.n");
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
    elf_to_file(binary, count, "sum.o");
}

/**
 * int sum(int a, int b) {
 *    return a + b
 * }
 * int foo = sum(1, 10)
 * print("%d", foo)
 */
static void test_lower_call() {
    amd64_register_init();
    opcode_init();
    amd64_lower_init();

    // sum closure
    // int sum(int a, int b) {
    //     return a + b
    //  }
    closure *sum_closure = NEW(closure);
    sum_closure->name = "sum";
    sum_closure->stack_length = 16;
    sum_closure->operates = list_op_new();


    // main closure
    closure *main_closure = NEW(closure);
    main_closure->name = "main";
    main_closure->stack_length = 16; // 所有局部变量合， 16字节对齐
    main_closure->operates = list_op_new();
    lir_operand *foo = test_lir_temp("foo", 8, -1); // var 的 size 是多少？

    //  call sum(1, 10) => foo
    lir_op *call_op = lir_op_call("sum", foo, 2,
                                  LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 10),
                                  LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 1));
    call_op->data_type = TYPE_INT;
    call_op->size = QWORD;


    lir_op *debug_op = lir_op_call("debug_printf", NULL, 2,
                                   LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, "sum(1, 10) =>  %d\n"),
                                   foo);
    list_op_push(main_closure->operates, call_op);
    list_op_push(main_closure->operates, debug_op);

    // lir_lower

    // exit

    elf_init("sum.n");
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
    elf_to_file(binary, count, "sum.o");
}

int main(void) {
    const struct CMUnitTest tests[] = {
//            cmocka_unit_test(test_lower_hello),
//            cmocka_unit_test(test_lower_debug_printf),
//            cmocka_unit_test(test_lower_sum),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}