#include "test.h"
#include <stdio.h>
//#include "src/lower/amd64/amd64.h"
#include "src/lower/amd64/builtin.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/elf/elf.h"

static void test_builtin_print() {
  register_init();
  opcode_init();

  // 数据段
  asm_var_decl decl = {
      .name = "str",
      .size = strlen("hello world!\n"),
      .value = (uint8_t *) "hello world!\n",
      .type = ASM_VAR_DECL_TYPE_STRING
  };
  asm_operand_t *string_entity_var = DISP_REG(rbp, -8);

  list *builtin_call_list = list_new();
  list_push(builtin_call_list, ASM_INST("label", { SYMBOL("_start", true, false) }));

  // 1. 创造一组字符串结构,并将字符串地址存储在 rbp - 8 中,当然不是在编译时创造，那没有任何意义
  // 这里还是考虑使用数据段临时构建，实际使用中通常都是
  // socket.write 写入到堆内存中
  list_push(builtin_call_list, ASM_INST("lea", { REG(rdi), SYMBOL(decl.name, false, false) }));
  list_push(builtin_call_list, ASM_INST("mov", { REG(rsi), UINT32(decl.size) }));
  list_push(builtin_call_list, ASM_INST("call", { SYMBOL(RUNTIME_CALL_STRING_NEW, true, false) }));
  list_push(builtin_call_list, ASM_INST("mov", { string_entity_var, REG(rax) }));

  // 调用内置函数 print
  list_push(builtin_call_list, ASM_INST("mov", { REG(rdi), string_entity_var }));
  list_push(builtin_call_list, ASM_INST("call", { SYMBOL("builtin_print", true, false) }));

  // 构造 elf
  elf_init("hello.n");
  // 数据段
  elf_var_decl_build(decl);
  // 代码段
  elf_text_inst_list_build(builtin_call_list);
  elf_text_inst_list_build(builtin_print());
  elf_text_inst_list_second_build();
  elf_t elf = elf_new();
  // 编码成二进制
  uint64_t count;
  uint8_t *binary = elf_encoding(elf, &count);
  // 输出到文件
  elf_to_file(binary, count, "hello.o");
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_builtin_print),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}