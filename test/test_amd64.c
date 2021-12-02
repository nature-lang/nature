#include "test.h"
#include "src/assembler/amd64/asm.h"

static int setup(void **state) {
  asm_init();
  return 0;
}

/**
 * mov rcx, [6379]
 */
static void test_mov_indirect_addr_to_reg_disp32_negative() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = -6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .src = indirect,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x8B, 0x88, 0x15, 0xE7, 0xFF, 0xFF};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * mov rcx, [6379]
 */
static void test_mov_indirect_addr_to_reg_basic_negative() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = -8;
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .src = indirect,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x8B, 0x48, 0xF8};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * mov rcx, [6379]
 */
static void test_mov_indirect_addr_to_reg_disp32() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .src = indirect,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x8B, 0x88, 0xEB, 0x18, 0x00, 0x00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

static void test_mov_reg_to_indirect_addr_disp32() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = 6379;
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .dst_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .dst = indirect,
      .src_type = ASM_OPERAND_TYPE_REG,
      .src = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x89, 0x88, 0xEB, 0x18, 0x00, 0x00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * mov rcx, [8]
 */
static void test_mov_indirect_addr_to_reg_basic() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";

  asm_indirect_addr *indirect = NEW(asm_direct_addr);
  indirect->reg = "rax";
  indirect->offset = 8;
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_INDIRECT_ADDR,
      .src = indirect,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x8B, 0x48, 0x08};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * mov rcx, [6379]
 */
static void test_mov_reg_to_direct_addr() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";
  asm_direct_addr *direct = NEW(asm_direct_addr);
  direct->addr = 6379; // 10 进制的 100
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .dst_type = ASM_OPERAND_TYPE_DIRECT_ADDR,
      .dst = direct,
      .src_type = ASM_OPERAND_TYPE_REG,
      .src = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x89, 0x0D, 0xEB, 0x18, 00, 00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * mov rcx, [6379]
 */
static void test_mov_direct_addr_to_reg() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";
  asm_direct_addr *direct = NEW(asm_direct_addr);
  direct->addr = 6379; // 10 进制的 100
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_DIRECT_ADDR,
      .src = direct,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0x8B, 0x0D, 0xEB, 0x18, 00, 00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

static void test_mov_rax_direct_addr() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rax";
  asm_direct_addr *direct = NEW(asm_direct_addr);
  direct->addr = 6379; // 10 进制的 100
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .dst_type = ASM_OPERAND_TYPE_DIRECT_ADDR,
      .dst = direct,
      .src_type = ASM_OPERAND_TYPE_REG,
      .src = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0xA3, 0xEB, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * mov rcx, [6379]
 */
static void test_mov_direct_addr_to_rax() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rax";
  asm_direct_addr *direct = NEW(asm_direct_addr);
  direct->addr = 6379; // 10 进制的 100
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_DIRECT_ADDR,
      .src = direct,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0xA1, 0xEB, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

/**
 * mov rcx, 4224967391
 */
static void test_mov_imm64_to_reg64() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rcx";
  asm_imm *imm = NEW(asm_imm);
  imm->value = 4224967391;
  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_IMM,
      .src = imm,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  elf_text_item actual = asm_inst_lower(inst);

  byte expect[30] = {0x48, 0xB9, 0xDF, 0xE2, 0xD3, 0xFB, 00, 00, 00, 00};
  for (int i = 0; i < 30; ++i) {
    assert_int_equal(actual.data[i], expect[i]);
  }
}

static void test_amd64_inst() {
  asm_reg *reg = NEW(asm_reg);
  reg->name = "rax";
  asm_imm *imm = NEW(asm_imm);
  imm->value = 65536;

  asm_inst inst = {
      .op = ASM_OP_TYPE_MOV,
      .size = 64,
      .src_type = ASM_OPERAND_TYPE_IMM,
      .src = imm,
      .dst_type = ASM_OPERAND_TYPE_REG,
      .dst = reg
  };

  asm_insts_push(inst);

  assert_int_equal(asm_insts.count, 1);

  asm_inst actual_inst = asm_insts.list[0];
  assert_int_equal(actual_inst.op, ASM_OP_TYPE_MOV);
  assert_int_equal(actual_inst.size, 64);
  assert_int_equal(actual_inst.src_type, ASM_OPERAND_TYPE_IMM);
  assert_memory_equal(actual_inst.src, imm, sizeof(asm_imm));
  assert_int_equal(actual_inst.dst_type, ASM_OPERAND_TYPE_REG);
  assert_memory_equal(actual_inst.dst, reg, sizeof(asm_reg));
}

static void test_amd64_var_decl() {
  char *name = "str_var";
  char *value = "i am in test_amd64_var_decl";

  asm_var_decl var_decl = {
      .name = name,
      .type = ASM_VAR_DECL_TYPE_STRING,
      .string_value = value,
  };

  asm_data_push(var_decl);

  assert_int_equal(asm_data.count, 1);
  asm_var_decl actual_var_decl = asm_data.list[0];
  assert_int_equal(actual_var_decl.type, ASM_VAR_DECL_TYPE_STRING);
  assert_string_equal(var_decl.name, name);
  assert_string_equal(var_decl.string_value, value);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_mov_indirect_addr_to_reg_basic_negative),
      cmocka_unit_test(test_mov_indirect_addr_to_reg_disp32_negative),
      cmocka_unit_test(test_mov_indirect_addr_to_reg_disp32),
      cmocka_unit_test(test_mov_indirect_addr_to_reg_basic),
      cmocka_unit_test(test_mov_reg_to_indirect_addr_disp32),
      cmocka_unit_test(test_mov_direct_addr_to_reg),
      cmocka_unit_test(test_mov_reg_to_direct_addr),
      cmocka_unit_test(test_mov_direct_addr_to_rax),
      cmocka_unit_test(test_mov_rax_direct_addr),
      cmocka_unit_test(test_mov_imm64_to_reg64),
      cmocka_unit_test(test_amd64_inst),
      cmocka_unit_test(test_amd64_var_decl)
  };

  return cmocka_run_group_tests(tests, setup, NULL);
}