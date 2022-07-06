#include "test.h"
#include <stdio.h>
#include "src/lower/amd64/builtin.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/elf/elf.h"

static void test_union_c() {
    register_init();
    opcode_init();
    // elf_init
    elf_init("union.n");

    asm_var_decl decl = {
            .name = "str",
            .size = strlen("hello world!\n"),
            .value = (uint8_t *) "hello world!\n",
            .type = ASM_VAR_DECL_TYPE_STRING
    };

    list *inst_list = list_new();
    // sys_write
    list_push(inst_list, ASM_INST("label", { SYMBOL("_start", true, false) }));
    list_push(inst_list, ASM_INST("mov", { REG(rbp), REG(rsp) }));

    list_push(inst_list, ASM_INST("call", { SYMBOL("length", true, false) }));
    list_push(inst_list, ASM_INST("mov", { DISP_REG(rbp, -8), REG(rax) }));
    list_push(inst_list, ASM_INST("mov", { REG(eax), UINT32(1) }));
    list_push(inst_list, ASM_INST("mov", { REG(rdi), UINT32(1) }));
    list_push(inst_list, ASM_INST("lea", { REG(rsi), SYMBOL(decl.name, false, false) }));
    list_push(inst_list, ASM_INST("mov", { REG(rdx), DISP_REG(rbp, -8) }));
    list_push(inst_list, ASM_INST("syscall", {}));


    // sys_exit
    list_push(inst_list, ASM_INST("mov", { REG(eax), UINT32(60) }));
    list_push(inst_list, ASM_INST("mov", { REG(rdi), UINT32(0) }));
    list_push(inst_list, ASM_INST("syscall", {}));

    // 数据段编译
    elf_var_decl_build(decl);
    // 代码段编译
    elf_text_inst_list_build(inst_list);
    elf_text_inst_list_second_build();

    // 构造 elf
    elf_t elf = elf_new();
    // 编码成二进制
    uint64_t count;
    uint8_t *binary = elf_encoding(elf, &count);
    // 输出到文件
    elf_to_file(binary, count, "union.o");
}

static void test_union_builtin_print() {
    register_init();
    opcode_init();

    // 数据段
    asm_var_decl decl = {
            .name = "str",
            .size = strlen("hello world!\n"),
            .value = (uint8_t *) "hello world!\n",
            .type = ASM_VAR_DECL_TYPE_STRING
    };
    asm_operand_t *string_entity_var = DISP_REG(rbp, -8);

    list *builtin_call_list = list_new();
    list_push(builtin_call_list, ASM_INST("label", { SYMBOL("_start", true, false) }));

    // 1. 创造一组字符串结构,并将字符串地址存储在 rbp - 8 中,当然不是在编译时创造，那没有任何意义
    // 这里还是考虑使用数据段临时构建，实际使用中通常都是
    // socket.write 写入到堆内存中
    list_push(builtin_call_list, ASM_INST("lea", { REG(rdi), SYMBOL(decl.name, false, false) }));
    list_push(builtin_call_list, ASM_INST("mov", { REG(rsi), UINT32(decl.size) }));
    list_push(builtin_call_list, ASM_INST("call", { SYMBOL(RUNTIME_CALL_STRING_NEW, true, false) }));
    list_push(builtin_call_list, ASM_INST("mov", { string_entity_var, REG(rax) }));

    // 调用内置函数 print
    list_push(builtin_call_list, ASM_INST("mov", { REG(rdi), string_entity_var }));
    list_push(builtin_call_list, ASM_INST("call", { SYMBOL("builtin_print", true, false) }));

    // 构造 elf
    elf_init("hello.n");
    // 数据段
    elf_var_decl_build(decl);
    // 代码段
    elf_text_inst_list_build(builtin_call_list);
    elf_text_inst_list_build(builtin_print());
    elf_text_inst_list_second_build();
    elf_t elf = elf_new();
    // 编码成二进制
    uint64_t count;
    uint8_t *binary = elf_encoding(elf, &count);
    // 输出到文件
    elf_to_file(binary, count, "hello.o");
}

static void test_builtin_print_target() {
    register_init();
    opcode_init();

    // 数据段

    // 构造 elf
    elf_init("builtin_print.n");
    // 代码段
    elf_text_inst_list_build(builtin_print());
    elf_text_inst_list_second_build();
    elf_t elf = elf_new();
    // 编码成二进制
    uint64_t count;
    uint8_t *binary = elf_encoding(elf, &count);
    // 输出到文件
    elf_to_file(binary, count, "builtin_print.o");
}

static void test_runtime_string_print() {
    register_init();
    opcode_init();

    // 数据段
    asm_operand_t *string_entity_var = DISP_REG(rbp, -8);

    list *inst_list = list_new();
    list_push(inst_list, ASM_INST("label", { LABEL("_start") }));

    // 1. 运行时调用 runtime_call gen_hello_world 在堆内存中生成字符串(响应结果在 rax(rax 和 eax 共用内存) 中)
    list_push(inst_list, ASM_INST("call", { LABEL("gen_hello_world") }));

    // 2. 调用 string_new 模拟 nature 中的字符串生成 string s = "hello world" = runtime_call string_new("xxx")
    list_push(inst_list, ASM_INST("mov", { REG(rsi), UINT32(13) }));
    list_push(inst_list, ASM_INST("mov", { REG(rdi), REG(rax) }));
    list_push(inst_list, ASM_INST("call", { LABEL(RUNTIME_CALL_STRING_NEW) })); // 生成 nature 字符串对象, 结果响应在 rax 中

    // 3. 准备调用 builtin print 输出字符串
    list_push(inst_list, ASM_INST("mov", { REG(rdi), REG(rax) })); // 参数传递
    list_push(inst_list, ASM_INST("call", { LABEL("builtin_print") }));

    // 4. sys_exit
    list_push(inst_list, ASM_INST("mov", { REG(eax), UINT32(60) }));
    list_push(inst_list, ASM_INST("mov", { REG(rdi), UINT32(0) }));
    list_push(inst_list, ASM_INST("syscall", {}));

    // 构造 elf
    elf_init("runtime_hello.n");
    // 代码段
    elf_text_inst_list_build(inst_list);
    elf_text_inst_list_build(builtin_print());
    elf_text_inst_list_second_build();
    elf_t elf = elf_new();
    // 编码成二进制
    uint64_t count;
    uint8_t *binary = elf_encoding(elf, &count);
    // 输出到文件
    elf_to_file(binary, count, "runtime_hello.o");
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_union_c),
            cmocka_unit_test(test_union_builtin_print),
            cmocka_unit_test(test_builtin_print_target),
            cmocka_unit_test(test_runtime_string_print),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}