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

  asm_operand_t *i = INDIRECT_REG(eax);
  asm_operand_t *d = DISP_REG(ebp, 10);
  asm_operand_t *s = SYMBOL("test", true, true);
  asm_operand_t *r = REG(r10);

  asm_inst_t inst = {
      .name = "push",
      .operands = {
          REG(rcx),
      },
      .count = 1
  };
  uint8_t byte_count;
  uint8_t *bytes = opcode_encoding(inst, &byte_count);
  uint8_t expect[] = {0x51};
  assert_memory_equal(bytes, expect, byte_count);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_elf_to_file),
      cmocka_unit_test(test_opcode_tree_build),
      cmocka_unit_test(test_opcode_init),
      cmocka_unit_test(test_opcode_encoding),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}