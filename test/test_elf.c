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

static void test_opcode_init() {
  opcode_init();

  assert_string_equal(opcode_tree_root->key, "root");
  assert_non_null(table_get(opcode_tree_root->succs, "mov"));
  assert_non_null(table_get(opcode_tree_root->succs, "push"));
}

static void test_opcode_encoding() {

  asm_operand_t *sib = SIB_REG(r10, r10d, 10);
  asm_operand_t *i = INDIRECT_REG(eax);
  asm_operand_t *d = DISP_REG(ebp, 10);
  asm_operand_t *s = SYMBOL("test", true, true);

  opcode_init();

  asm_inst_t inst = {
      .name = "push",
      .operands = {
          rax
      },
      .count = 1
  };
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_elf_to_file),
      cmocka_unit_test(test_opcode_init),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}