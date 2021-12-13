#include "test.h"
#include "src/assembler/amd64/asm.h"

static int setup(void **state) {
  asm_init();
  return 0;
}

static void test_div_reg64() {
  asm_reg *src_reg = NEW(asm_reg);
  src_reg->name = "rdx";

  asm_inst inst = {
      .op = ASM_OP_TYPE_DIV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_REG,
      .src = src_reg,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0xF7, 0xFA};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * idiv QWORD PTR [rcx+6379]
 */
static void test_div_indirect_addr() {
  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rcx";
  indirect->offset = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_DIV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .src = indirect,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0xF7, 0xB9, 0xEB, 0x18, 0x00, 0x00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_div_reg64),
      cmocka_unit_test(test_div_indirect_addr),
  };

  return cmocka_run_group_tests(tests, setup, NULL);
}