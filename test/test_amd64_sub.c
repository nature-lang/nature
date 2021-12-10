#include "test.h"
#include "src/assembler/amd64/asm.h"

static int setup(void **state) {
  asm_init();
  return 0;
}

static void test_sub_reg64_to_reg64() {
  asm_reg *src_reg = NEW(asm_reg);
  src_reg->name = "rax";

  asm_reg *dst_reg = NEW(asm_reg);
  dst_reg->name = "rcx";

  asm_inst inst = {
      .op = ASM_OP_TYPE_SUB,
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

static void test_sub_imm32_to_reg64() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";
  asm_imm *imm = NEW(asm_imm);
  imm->value = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_SUB,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_IMM,
      .src = imm,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x81, 0xE9, 0xEB, 0x18, 00, 00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

static void test_sub_imm32_to_rax() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rax";
  asm_imm *imm = NEW(asm_imm);
  imm->value = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_SUB,
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

/**
 * sub rcx,[rax-6379]
 */
static void test_sub_indirect_addr_to_reg_disp32_negative() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = -6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_SUB,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .src = indirect,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x03, 0x88, 0x15, 0xE7, 0xFF, 0xFF};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * sub rcx, [rax-8]
 */
static void test_sub_indirect_addr_to_reg_basic_negative() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = -8;
  asm_inst inst = {
      .op = ASM_OP_TYPE_SUB,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .src = indirect,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x03, 0x48, 0xF8};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * sub rcx, [rax+6379]
 */
static void test_sub_indirect_addr_to_reg_disp32() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_SUB,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .src = indirect,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x03, 0x88, 0xEB, 0x18, 0x00, 0x00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * sub [rax+6379],rcx
 */
static void test_sub_reg_to_indirect_addr_disp32() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_SUB,
      .size = 64,
      .dst_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .dst = indirect,
      .src_type = ASM_OPERAND_TYPE_REG,
      .src = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x01, 0x88, 0xEB, 0x18, 0x00, 0x00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * sub rcx, [rax+8]
 */
static void test_sub_indirect_addr_to_reg_basic() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = 8;
  asm_inst inst = {
      .op = ASM_OP_TYPE_SUB,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .src = indirect,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x03, 0x48, 0x08};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

int main(void) {
  const struct CMUnitTest tests[] = {
//      cmocka_unit_test(test_sub_imm32_to_rax),
      cmocka_unit_test(test_sub_imm32_to_reg64),
//      cmocka_unit_test(test_sub_reg64_to_reg64),
//      cmocka_unit_test(test_sub_indirect_addr_to_reg_disp32_negative),
//      cmocka_unit_test(test_sub_indirect_addr_to_reg_basic_negative),
//      cmocka_unit_test(test_sub_indirect_addr_to_reg_disp32),
//      cmocka_unit_test(test_sub_reg_to_indirect_addr_disp32),
//      cmocka_unit_test(test_sub_indirect_addr_to_reg_basic),
  };

  return cmocka_run_group_tests(tests, setup, NULL);
}