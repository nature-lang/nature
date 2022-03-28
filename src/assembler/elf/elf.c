#include "lib_elf.h"
#include "elf.h"
#include "src/lib/helper.h"

/**
 *
 * 输入：text 代码段 (byte 列表)
 * .data 变量定义段是否需要？
 */
void elf_target_build() {
  // elf header 文件头表构建

  // .text 代码段

  // 重定位表

  // section header table 段表构建
}

void elf_asm_encoding(asm_inst_t asm_inst) {
  uint64_t *offset = elf_new_current_offset();
  if (strequal(asm_inst.name, "label")) {
    // text 中唯一需要注册到符号表的数据, 且不需要编译进 elf_text_item
    asm_operand_symbol_t *s = asm_inst.operands[0]->value;
    elf_symbol_t symbol = {
        .name = s->name,
        .type = ELF_SYMBOL_TYPE_FN,
        .section = ELF_SECTION_TEXT,
        .offset = offset,
        .size = 0,
        .is_rel = false,
    };
    elf_symbol_insert(symbol);

  }

  uint8_t data[30] = {0};
  uint8_t count = 0;
  opcode_encoding(asm_inst, data, &count);
}
