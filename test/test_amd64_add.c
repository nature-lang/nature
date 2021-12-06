#include "test.h"
#include "src/assembler/amd64/asm.h"

static int setup(void **state) {
  asm_init();
  return 0;
}

static void test_add_reg64_to_reg64() {
  asm_reg *src_reg = NEW(asm_reg);
  src_reg->name = "rax";

  asm_reg *dst_reg = NEW(asm_reg);
  dst_reg->name = "rcx";

  asm_inst inst = {
      .op = ASM_OP_TYPE_ADD,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_REG,
      .src = src_reg,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = dst_reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x01, 0xC1};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

static void test_add_imm32_to_reg64() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";
  asm_imm *imm = NEW(asm_imm);
  imm->value = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_ADD,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_IMM,
      .src = imm,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x81, 0xC1, 0xEB, 0x18, 00, 00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

static void test_add_imm32_to_rax() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rax";
  asm_imm *imm = NEW(asm_imm);
  imm->value = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_ADD,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_IMM,
      .src = imm,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x05, 0xEB, 0x18, 00, 00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

static void test_add_direct_addr_to_reg() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";
  asm_direct_addr *direct = NEW(asm_direct_addr);
  direct->addr = 6379; // 10 进制的 100
  asm_inst inst = {
      .op = ASM_OP_TYPE_ADD,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_DIRECT_ADDR,
      .src = direct,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x03, 0x0D, 0xE4, 0x18, 00, 00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_add_imm32_to_rax),
      cmocka_unit_test(test_add_imm32_to_reg64),
      cmocka_unit_test(test_add_reg64_to_reg64),
  };

  return cmocka_run_group_tests(tests, setup, NULL);
}