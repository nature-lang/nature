#include "test.h"
#include <stdio.h>
#include "runtime/builtin/builtin.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/elf/elf.h"
#include "src/lower/amd64/amd64.h"
#include "src/lir/lir.h"

static lir_operand_var *test_lir_operand_var(char *ident, int stack_frame_offset, uint8_t size) {
    lir_operand_var *var = NEW(lir_operand_var);
    var->old = ident;
    var->ident = ident;
    if (stack_frame_offset >= 0) {
        var->local->stack_frame_offset = stack_frame_offset;
    }
    var->size = size;
}

static lir_operand *test_lir_temp(char *ident, int stack_frame_offset) {
    return LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, test_lir_operand_var(ident, stack_frame_offset, QWORD));
}

static list *test_elf_main_insts(uint8_t stack_offset) {
    list *insts = list_new();
//    list_push(insts, ASM_INST("label", { SYMBOL("_start", true, false) }));
    list_push(insts, ASM_INST("label", { SYMBOL("main", false) }));
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
    lir_operand *temp_var = test_lir_temp("temp_1", 8); // var 的 size 是多少？
    lir_op *string_new_op = lir_op_call(RUNTIME_CALL_STRING_NEW, temp_var, 2,
                                        LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, str),
                                        LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, strlen(str)));

    lir_op *print_op = lir_op_call("builtin_print", NULL, 1, temp_var);

    // lir_lower
    list_append(insts, amd64_lower_op(c, string_new_op));
    list_append(insts, amd64_lower_op(c, print_op));

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
    list_append(insts, amd64_lower_op(c, debug_printf_op));

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
    lir_operand *temp_var = test_lir_temp("sum_1", 8); // var 的 size 是多少？
    lir_op *sum_op = lir_op_new(LIR_OP_TYPE_ADD,
                                LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 22),
                                LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 33),
                                temp_var);

    lir_op *debug = lir_op_call("debug_printf", NULL, 2,
                                LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, "add 22 + 33 = %d\n"),
                                temp_var);

    // lir_lower
    list_append(insts, amd64_lower_op(c, sum_op));
    list_append(insts, amd64_lower_op(c, debug));

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
    sum_closure->end_label = "sum_end";
    sum_closure->stack_length = 32;
    lir_operand_var *a = test_lir_operand_var("a", 8, QWORD);
    lir_operand_var *b = test_lir_operand_var("b", 16, QWORD);
    lir_operand_var *temp = test_lir_operand_var("temp_var", 24, QWORD);
//    lir_vars vars = {.count = 2, .list = {a, b}};
//    sum_closure->formal_params = vars;
    sum_closure->operates = list_new();
    lir_op *sum_op = lir_op_new(LIR_OP_TYPE_ADD,
                                LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, a),
                                LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, b),
                                LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, temp));
    lir_op *return_op = lir_op_new(LIR_OP_TYPE_RETURN, NULL, NULL, LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, temp));
    list_push(sum_closure->operates, sum_op);
    list_push(sum_closure->operates, return_op);

//   test
//    char *str = "hello world";
//    lir_op *debug_printf_op = lir_op_call("debug_printf",
//                                          NULL, 2, LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, str)
//    );
//    list_push(sum_closure->operates, debug_printf_op);

    // main closure
    closure *main_closure = NEW(closure);
    main_closure->name = "main";
    main_closure->end_label = "main_end";
    main_closure->stack_length = 16; // 所有局部变量合， 16字节对齐
    main_closure->operates = list_new();
    lir_operand *foo = test_lir_temp("foo", 8);

    //  call sum(1, 10) => foo
    lir_op *call_op = lir_op_call("sum", foo, 2,
                                  LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 1),
                                  LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 10));


    lir_op *debug_op = lir_op_call("debug_printf", NULL, 2,
                                   LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, "sum(1, 10) =>  %d\n"),
                                   foo);

    list_push(main_closure->operates, call_op);
    list_push(main_closure->operates, debug_op);

    list *insts = amd64_lower_closure(main_closure);
    list_append(insts, amd64_lower_closure(sum_closure));
//    list *insts = amd64_lower_closure(sum_closure);


    elf_init("call.n");
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
    elf_to_file(binary, count, "call.o");
}


static void test_lower_if() {
    amd64_register_init();
    opcode_init();
    amd64_lower_init();


    // main closure
    closure *main_closure = NEW(closure);
    main_closure->name = "main";
    main_closure->end_label = "main_end";
    main_closure->stack_length = 16; // 所有局部变量合， 16字节对齐
    main_closure->operates = list_new();
    lir_operand *foo = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, test_lir_operand_var("foo", 8, QWORD));
    lir_operand *cmp_res = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, test_lir_operand_var("cmp_res", 16, BYTE));

    // 编写指令
    // mov [rbp+8],1
    lir_op *mov_op = lir_op_move(foo, LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT32, int_value, 2));


    // cmp foo，1 => cmp_res
    lir_op *cmp_gt_op = lir_op_new(LIR_OP_TYPE_GT, foo, LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 1), cmp_res);

    // cmp goto
    lir_op *cmp_goto_op = lir_op_new(LIR_OP_TYPE_CMP_GOTO, LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool_value, false),
                                     cmp_res, lir_new_label_operand(END_IF_IDENT, true));

    //  true 条件
    lir_op *true_op = lir_op_call("debug_printf", NULL, 1,
                                  LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, "foo > 1\n"));

    // return to end fn
    lir_op *goto_end = lir_op_goto(lir_new_label_operand("end_main", true));

    lir_op *label_end_if_op = lir_op_label(END_IF_IDENT, false);
    lir_op *false_op = lir_op_call("debug_printf", NULL, 1,
                                   LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, "foo <= 1\n"));

    lir_op *label_end_main = lir_op_label("end_main", false);

    list_push(main_closure->operates, mov_op);
    list_push(main_closure->operates, cmp_gt_op);
    list_push(main_closure->operates, cmp_goto_op);
    list_push(main_closure->operates, true_op);
    list_push(main_closure->operates, goto_end);
    list_push(main_closure->operates, label_end_if_op);
    list_push(main_closure->operates, false_op);
    list_push(main_closure->operates, label_end_main);

    list *insts = amd64_lower_closure(main_closure);

//    insts = amd64_lower_op(main_closure, mov_op);

    elf_init("if.n");
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
    elf_to_file(binary, count, "if.o");
}


int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_lower_hello),
            cmocka_unit_test(test_lower_debug_printf),
            cmocka_unit_test(test_lower_sum),
            cmocka_unit_test(test_lower_call),
            cmocka_unit_test(test_lower_if),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}