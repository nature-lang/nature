#include "test.h"
#include "src/assembler/amd64/asm.h"

static int setup(void **state) {
  asm_init();
  return 0;
}

static void test_jmp_rel32_nop() {
  asm_symbol *symbol = NEW(asm_symbol);
  symbol->name = "test";
  asm_inst inst = {
      .op = ASM_OP_TYPE_JMP,
      .size = 64,
      .dst_type = ASM_OPERAND_TYPE_SYMBOL,
      .dst = symbol,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0xE9, 0x90, 0x90, 0x90, 0x90};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

static void test_jmp_reg64() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rdx";

  asm_inst inst = {
      .op = ASM_OP_TYPE_JMP,
      .size = 64,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0xF7, 0xFA};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * jmp QWORD PTR [rdx]
 */
static void test_jmp_indirect_addr() {
  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rdx";
  indirect->offset = 0;
  asm_inst inst = {
      .op = ASM_OP_TYPE_JMP,
      .size = 64,
      .dst_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .dst = indirect,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0xFF, 0x22};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * jmp QWORD PTR [rcx+6379]
 */
static void test_jmp_indirect_addr_offset32() {
  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rcx";
  indirect->offset = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_JMP,
      .size = 64,
      .dst_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .dst = indirect,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0xFF, 0xA1, 0xEB, 0xEB, 0x18, 0x00, 0x00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_jmp_reg64),
      cmocka_unit_test(test_jmp_indirect_addr),
      cmocka_unit_test(test_jmp_indirect_addr_offset32),
      cmocka_unit_test(test_jmp_rel32_nop),
  };

  return cmocka_run_group_tests(tests, setup, NULL);
}