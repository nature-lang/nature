#include "test.h"
#include "src/assembler/amd64/asm.h"

static int setup(void **state) {
  asm_init();
  return 0;
}

static void test_set_al() {
  asm_reg *src_reg = NEW(asm_reg);
  src_reg->name = "al";

  asm_inst inst = {
      .op = ASM_OP_TYPE_SETG,
      .size = 8,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = src_reg,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x0F, 0x9F, 0xC0};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

static void test_set_ah() {
  asm_reg *src_reg = NEW(asm_reg);
  src_reg->name = "ah";

  asm_inst inst = {
      .op = ASM_OP_TYPE_SETG,
      .size = 8,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = src_reg,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x0F, 0x9F, 0xC4};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * setg [rcx+6379]
 */
static void test_set_indirect_addr() {
  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rcx";
  indirect->offset = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_SETG,
      .size = 8,
      .dst_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .dst = indirect,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x0F, 0x9F, 0x81, 0xEB, 0x18, 0x00, 0x00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_set_indirect_addr),
      cmocka_unit_test(test_set_al),
      cmocka_unit_test(test_set_ah),
  };

  return cmocka_run_group_tests(tests, setup, NULL);
}