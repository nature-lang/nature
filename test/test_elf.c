#include "test.h"
#include <stdio.h>
#include "src/assembler/elf/elf.h"
#include "src/lib/helper.h"
#include "src/assembler/amd64/register.h"
#include "runtime/builtin/builtin.h"
#include "src/lower/amd64/amd64.h"
#include "src/lir/lir.h"


static list *test_gen_asm_print() {
    //static list *asm_builtin_print() {
    asm_operand_t *string_entity_var = DISP_REG(rbp, -8, QWORD);
    asm_operand_t *string_addr_var = DISP_REG(rbp, -16, QWORD);
    asm_operand_t *string_len_var = DISP_REG(rbp, -24, QWORD);

    list *inst_list = list_new();
    list_push(inst_list, ASM_INST("label", { SYMBOL("builtin_print", false) }));
    list_push(inst_list, ASM_INST("push", { REG(rbp) }));
    list_push(inst_list, ASM_INST("mov", { REG(rbp), REG(rsp) })); // 保存栈指针
    list_push(inst_list, ASM_INST("sub", { REG(rsp), UINT32(24) })); // 防止其他函数调用占用这一段栈空间

    list_push(inst_list, ASM_INST("mov", { string_entity_var, REG(rdi) }));

    // runtime_call string_point => addr
    list_push(inst_list, ASM_INST("mov", { REG(rdi), string_entity_var }));
    list_push(inst_list, ASM_INST("call", { LABEL(RUNTIME_CALL_STRING_ADDR) }));
    list_push(inst_list, ASM_INST("mov", { string_addr_var, REG(rax) }));


    // runtime_call string_len => len
    list_push(inst_list, ASM_INST("mov", { REG(rdi), string_entity_var }));
    list_push(inst_list, ASM_INST("call", { SYMBOL(RUNTIME_CALL_STRING_LENGTH, false) }));
    list_push(inst_list, ASM_INST("mov", { string_len_var, REG(rax) }));


    // syscall
    list_push(inst_list, ASM_INST("mov", { REG(rax), UINT32(1) })); // 声明系统调用编号 write
    list_push(inst_list, ASM_INST("mov", { REG(rdi), UINT32(1) })); // 参数 1： 声明系统输出为标准输出 STDOUT_FILENO
    list_push(inst_list, ASM_INST("mov", { REG(rsi), string_addr_var })); // 参数 2: 字符串地址
    list_push(inst_list, ASM_INST("mov", { REG(rdx), string_len_var })); // 参数 3：字符串长度
    list_push(inst_list, ASM_INST("syscall", {})); // syscall

    // linux abi 要求 callee 在结束函数块前恢复 rsp 和 rpb
    // pop rbp 将恢复 caller 的栈帧赋, 且rsp - 8,
    list_push(inst_list, ASM_INST("mov", { REG(rsp), REG(rbp) }));
    list_push(inst_list, ASM_INST("pop", { REG(rbp) }));
    list_push(inst_list, ASM_INST("ret", {}));

    return inst_list;
}


static void test_elf_to_file() {
    uint8_t binary[5] = {1, 2, 3, 4, 5};
    filename = str_connect("test.n", ".o");
    elf_to_file(binary, 5, filename);
}

static opcode_tree_node_t *opcode_node_new() {
    opcode_tree_node_t *node = NEW(opcode_tree_node_t);
    node->key = "";
    node->insts = (insts_t) {
            .count = 0,
            .list = malloc(sizeof(inst_t) * 10),
    };
    node->succs = table_new();
    return node;
}

static void test_opcode_tree_build() {
    opcode_tree_node_t *parent = opcode_node_new();

    asm_keys_t keys = operand_low_to_high(OPERAND_TYPE_R64);
    uint16_t key_int = keys.list[0];
    assert_int_equal(key_int, 264);
    char *key = itoa(key_int);
    assert_string_equal(key, "264");
    table_set(parent->succs, key, "testvalue");
    void *v1 = table_get(parent->succs, key);
    assert_non_null(v1);
    void *v2 = table_get(parent->succs, "264");
    assert_non_null(v2);
}

static void test_opcode_init() {
    opcode_init();

    assert_string_equal(opcode_tree_root->key, "root");
    assert_non_null(table_get(opcode_tree_root->succs, "mov"));
    assert_non_null(table_get(opcode_tree_root->succs, "push"));

    opcode_tree_node_t *node = table_get(opcode_tree_root->succs, "push");
    uint16_t key_raw = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, QWORD);
    char *key = itoa(key_raw);
    opcode_tree_node_t *operand_node = table_get(node->succs, key);
    assert_non_null(operand_node);
}

static void test_opcode_encoding() {
    amd64_register_init();
    opcode_init();
    asm_inst_t inst = {
            .name = "push",
            .operands = {
                    REG(rcx),
            },
            .count = 1
    };
    uint8_t byte_count;
    uint8_t *actual = opcode_encoding(inst, &byte_count);
    uint8_t expect[] = {0x51};
    assert_memory_equal(actual, expect, byte_count);

    asm_inst_t *mov_eax_4 = ASM_INST("mov", { REG(eax), UINT32(1) });
    uint8_t *actual2 = opcode_encoding(*mov_eax_4, &byte_count);
    uint8_t expect2[] = {0xB8, 0x01, 0x00, 0x00, 0x00};
    assert_memory_equal(actual2, expect2, byte_count);

    asm_inst_t *mov_1_rdi = ASM_INST("mov", { REG(rdi), UINT32(1) });
    uint8_t *actual3 = opcode_encoding(*mov_1_rdi, &byte_count);
    uint8_t expect3[] = {0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00};
    assert_memory_equal(actual3, expect3, byte_count);

    asm_inst_t *mov_rip_rsi = ASM_INST("mov", { REG(rsi), RIP_RELATIVE(0) });
    uint8_t *actual4 = opcode_encoding(*mov_rip_rsi, &byte_count);
    uint8_t expect4[] = {0x48, 0x8B, 0x35, 0x00, 0x00, 0x00, 0x00};
    assert_memory_equal(actual4, expect4, byte_count);

    asm_inst_t *syscall = ASM_INST("syscall", {});
    uint8_t *actual5 = opcode_encoding(*syscall, &byte_count);
    uint8_t expect5[] = {0x0F, 0x05};
    assert_memory_equal(actual5, expect5, byte_count);

    asm_inst_t *mov_rax_disp = ASM_INST("mov", { DISP_REG(rbp, -8, QWORD), REG(rax) });
    uint8_t *actual6 = opcode_encoding(*mov_rax_disp, &byte_count);
    // 0x43 => mod:01 reg:000 rm:101
    uint8_t expect6[] = {0x48, 0x89, 0x45, 0xF8};
    assert_memory_equal(actual6, expect6, byte_count);

    asm_inst_t *add_disp_rax = ASM_INST("add", { REG(rax), DISP_REG(rbp, -8, QWORD), });
    uint8_t *actual7 = opcode_encoding(*add_disp_rax, &byte_count);
    // 0x43 => mod:01 reg:000 rm:101
    uint8_t expect7[] = {0x48, 0x03, 0x45, 0xF8};
    assert_memory_equal(actual7, expect7, byte_count);

    asm_inst_t *mov_disp_rax = ASM_INST("mov", { REG(rax), DISP_REG(rbp, -8, QWORD), });
    uint8_t *actual8 = opcode_encoding(*mov_disp_rax, &byte_count);
    // 0x43 => mod:01 reg:000 rm:101
    uint8_t expect18[] = {0x48, 0x8b, 0x45, 0xF8};
    assert_memory_equal(actual8, expect18, byte_count);

    asm_inst_t *mov_xmm0_disp = ASM_INST("mov", { REG(xmm0), DISP_REG(rbp, -8, QWORD), });
    uint8_t *actual9 = opcode_encoding(*mov_xmm0_disp, &byte_count);
    // 0x43 => mod:01 reg:000 rm:101
    uint8_t expect19[] = {0xF2, 0x0F, 0x10, 0x45, 0xF8};
    assert_memory_equal(actual9, expect19, byte_count);

    uint8_t expect10[] = {0xF2, 0x0F, 0x10, 0xC1};
    uint8_t *actual10 = opcode_encoding(*ASM_INST("mov", { REG(xmm0), REG(xmm1) }), &byte_count);
    assert_memory_equal(actual10, expect10, byte_count);

    uint8_t expect11[] = {0xF2, 0x0F, 0x11, 0x4D, 0xF8};
    uint8_t *actual11 = opcode_encoding(*ASM_INST("mov", { DISP_REG(rbp, -8, QWORD), REG(xmm1) }), &byte_count);
    assert_memory_equal(actual11, expect11, byte_count);
}

static void test_hello_world() {
    amd64_register_init();
    opcode_init();
    // elf_init
    elf_init("hello.n");

    asm_var_decl decl = {
            .name = "str",
            .size = strlen("hello world!\n"),
            .value = (uint8_t *) "hello world!\n",
            .type = ASM_VAR_DECL_TYPE_STRING
    };

    // sys_write
    asm_inst_t *start = ASM_INST("label", { SYMBOL("_start", false) });
    asm_inst_t *mov_eax_4 = ASM_INST("mov", { REG(eax), UINT32(1) });
    asm_inst_t *mov_1_rdi = ASM_INST("mov", { REG(rdi), UINT32(1) });
    asm_inst_t *mov_str_rsi = ASM_INST("lea", { REG(rsi), SYMBOL(decl.name, false) });
    asm_inst_t *mov_len_rdx = ASM_INST("mov", { REG(rdx), UINT32(decl.size) });
    asm_inst_t *syscall = ASM_INST("syscall", {});

    // sys_exit
    asm_inst_t *mov_60_eax = ASM_INST("mov", { REG(eax), UINT32(60) });
    asm_inst_t *mov_0_rdi = ASM_INST("mov", { REG(rdi), UINT32(0) });
    // encoding
    list *inst_list = list_new();
    list_push(inst_list, start);
    list_push(inst_list, mov_eax_4);
    list_push(inst_list, mov_1_rdi);
    list_push(inst_list, mov_str_rsi);
    list_push(inst_list, mov_len_rdx);
    list_push(inst_list, syscall);
    list_push(inst_list, mov_60_eax);
    list_push(inst_list, mov_0_rdi);
    list_push(inst_list, syscall);

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
    elf_to_file(binary, count, "hello.o");
}

static void test_call() {
    amd64_register_init();
    opcode_init();
    // elf_init
    elf_init("call.n");

    asm_var_decl decl = {
            .name = "str",
            .size = strlen("hello world!\n"),
            .value = (uint8_t *) "hello world!\n",
            .type = ASM_VAR_DECL_TYPE_STRING
    };

    // sys_write
    asm_inst_t *start_label = ASM_INST("label", { SYMBOL("_start", false) });
    asm_inst_t *call_hello = ASM_INST("call", { SYMBOL("hello", false) });

    asm_inst_t *hello_label = ASM_INST("label", { SYMBOL("hello", false) });
    asm_inst_t *mov_eax_4 = ASM_INST("mov", { REG(eax), UINT32(1) });
    asm_inst_t *mov_1_rdi = ASM_INST("mov", { REG(rdi), UINT32(1) });
    asm_inst_t *mov_str_rsi = ASM_INST("lea", { REG(rsi), SYMBOL(decl.name, false) });
    asm_inst_t *mov_len_rdx = ASM_INST("mov", { REG(rdx), UINT32(decl.size) });
    asm_inst_t *syscall = ASM_INST("syscall", {});

    // sys_exit
    asm_inst_t *mov_60_eax = ASM_INST("mov", { REG(eax), UINT32(60) });
    asm_inst_t *mov_0_rdi = ASM_INST("mov", { REG(rdi), UINT32(0) });


    // encoding
    list *inst_list = list_new();

    list_push(inst_list, hello_label);
    list_push(inst_list, mov_eax_4);
    list_push(inst_list, mov_1_rdi);
    list_push(inst_list, mov_str_rsi);
    list_push(inst_list, mov_len_rdx);
    list_push(inst_list, syscall);
    list_push(inst_list, mov_60_eax);
    list_push(inst_list, mov_0_rdi);
    list_push(inst_list, syscall);

    list_push(inst_list, start_label);
    list_push(inst_list, call_hello);

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
    elf_to_file(binary, count, "call.o");
}


static void test_union_c() {
    amd64_register_init();
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
    list_push(inst_list, ASM_INST("label", { SYMBOL("_start", false) }));
    list_push(inst_list, ASM_INST("mov", { REG(rbp), REG(rsp) }));

    list_push(inst_list, ASM_INST("call", { SYMBOL("length", false) }));
    list_push(inst_list, ASM_INST("mov", { DISP_REG(rbp, -8, QWORD), REG(rax) }));
    list_push(inst_list, ASM_INST("mov", { REG(eax), UINT32(1) }));
    list_push(inst_list, ASM_INST("mov", { REG(rdi), UINT32(1) }));
    list_push(inst_list, ASM_INST("lea", { REG(rsi), SYMBOL(decl.name, false) }));
    list_push(inst_list, ASM_INST("mov", { REG(rdx), DISP_REG(rbp, -8, QWORD) }));
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
    amd64_register_init();
    opcode_init();

    // 数据段
    asm_var_decl decl = {
            .name = "str",
            .size = strlen("hello world!\n"),
            .value = (uint8_t *) "hello world!\n",
            .type = ASM_VAR_DECL_TYPE_STRING
    };
    asm_operand_t *string_entity_var = DISP_REG(rbp, -8, QWORD);

    list *builtin_call_list = list_new();
    list_push(builtin_call_list, ASM_INST("label", { SYMBOL("_start", false) }));

    // 1. 创造一组字符串结构,并将字符串地址存储在 rbp - 8 中,当然不是在编译时创造，那没有任何意义
    // 这里还是考虑使用数据段临时构建，实际使用中通常都是
    // socket.write 写入到堆内存中
    list_push(builtin_call_list, ASM_INST("lea", { REG(rdi), SYMBOL(decl.name, false) }));
    list_push(builtin_call_list, ASM_INST("mov", { REG(rsi), UINT32(decl.size) }));
    list_push(builtin_call_list, ASM_INST("call", { SYMBOL(RUNTIME_CALL_STRING_NEW, false) }));
    list_push(builtin_call_list, ASM_INST("mov", { string_entity_var, REG(rax) }));

    // 调用内置函数 print
    list_push(builtin_call_list, ASM_INST("mov", { REG(rdi), string_entity_var }));
    list_push(builtin_call_list, ASM_INST("call", { SYMBOL("builtin_print", false) }));

    // 构造 elf
    elf_init("hello.n");
    // 数据段
    elf_var_decl_build(decl);
    // 代码段
    elf_text_inst_list_build(builtin_call_list);
    elf_text_inst_list_build(test_gen_asm_print());
    elf_text_inst_list_second_build();
    elf_t elf = elf_new();
    // 编码成二进制
    uint64_t count;
    uint8_t *binary = elf_encoding(elf, &count);
    // 输出到文件
    elf_to_file(binary, count, "hello.o");
}

static void test_builtin_print_target() {
    amd64_register_init();
    opcode_init();

    // 构造 elf
    elf_init("builtin_print.n");
    // 代码段
    elf_text_inst_list_build(test_gen_asm_print());
    elf_text_inst_list_second_build();
    elf_t elf = elf_new();
    // 编码成二进制
    uint64_t count;
    uint8_t *binary = elf_encoding(elf, &count);
    // 输出到文件
    elf_to_file(binary, count, "builtin_print.o");
}

static void test_runtime_string_print() {
    amd64_register_init();
    opcode_init();

    // 数据段
    asm_operand_t *string_entity_var = DISP_REG(rbp, -8, QWORD);

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
    elf_text_inst_list_build(test_gen_asm_print());
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
            cmocka_unit_test(test_elf_to_file),
            cmocka_unit_test(test_opcode_tree_build),
            cmocka_unit_test(test_opcode_init),
            cmocka_unit_test(test_opcode_encoding),
            cmocka_unit_test(test_hello_world),
            cmocka_unit_test(test_call),
            cmocka_unit_test(test_union_c),
            cmocka_unit_test(test_union_builtin_print),
            cmocka_unit_test(test_builtin_print_target),
            cmocka_unit_test(test_runtime_string_print),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}