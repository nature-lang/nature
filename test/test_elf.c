#include "test.h"
#include <stdio.h>
#include "src/assembler/elf/elf.h"
#include "src/lib/helper.h"
#include "src/assembler/amd64/register.h"

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
  register_init();
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

}

static void test_hello_world() {
  register_init();
  opcode_init();
  // elf_init
  elf_init("hello.n");

  asm_var_decl decl = {
      .name = "str",
      .size = strlen("hello world"),
      .value = (uint8_t *) "hello world",
      .type = ASM_VAR_DECL_TYPE_STRING
  };

  // sys_write
  asm_inst_t *start = ASM_INST("label", { SYMBOL("_start", true, false) });
  asm_inst_t *mov_eax_4 = ASM_INST("mov", { REG(eax), UINT32(1) });
  asm_inst_t *mov_1_rdi = ASM_INST("mov", { REG(rdi), UINT32(1) });
  asm_inst_t *mov_str_rsi = ASM_INST("mov", { REG(rsi), SYMBOL(decl.name, false, false) });
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

int main(void) {
  const struct CMUnitTest tests[] = {
//      cmocka_unit_test(test_elf_to_file),
//      cmocka_unit_test(test_opcode_tree_build),
//      cmocka_unit_test(test_opcode_init),
//      cmocka_unit_test(test_opcode_encoding),
      cmocka_unit_test(test_hello_world),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}