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

static void test_lower_hello_world() {
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
    lir_op *string_new_op = lir_runtime_call(RUNTIME_CALL_STRING_NEW, temp_var, 2,
                                             LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, str),
                                             LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, strlen(str)));

    lir_op *print_op = lir_builtin_call("builtin_print", NULL, 1, temp_var);

    // lir_lower
    list_merge(insts, amd64_lower(c, string_new_op));
    list_merge(insts, amd64_lower(c, print_op));

    // exit
    list_merge(insts, test_elf_return_insts());

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

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_lower_hello_world),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}