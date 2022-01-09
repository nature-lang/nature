#include "test.h"
#include "src/assembler/amd64/asm.h"

static int setup(void **state) {
  asm_init();
  return 0;
}

static void test_je_rel32_nop() {
  asm_symbol *symbol = NEW(asm_symbol);
  symbol->name = "test";
  asm_inst inst = {
      .op = ASM_OP_TYPE_JE,
      .size = 64,
      .dst_type = ASM_OPERAND_TYPE_SYMBOL,
      .dst = symbol,
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x0F, 0x84, 0x90, 0x90, 0x90, 0x90};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_je_rel32_nop),
  };

  return cmocka_run_group_tests(tests, setup, NULL);
}